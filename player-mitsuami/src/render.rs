//! The picture on a thread of its own (2026-10-16). Each draw acquires the
//! surface's next image, and that waits for the screen: up to a vblank on
//! Direct3D 12, 16.5 ms for a drawable on macOS. Drawn on the UI thread, a
//! guest drawing at 60 Hz left it nothing between frames, and the pointer,
//! the menus and Close lagged (user, 2026-10-16). Here the wait is this
//! thread's, and the UI thread only asks: a draw, a size, a scale, a shot.
//!
//! The thread owns the `Gpu`. QEMU's waker (`Shared::notifier`) wakes it;
//! it imports what QEMU announced, acquires, samples the newest guest frame
//! under the session's lock (acquire before sampling, doc 03), presents,
//! and then wakes the UI thread for what follows a publish there (the
//! cursor, the pads, the window's floor). What the window reads of the
//! picture (where it is, its size) is a `GpuView` copied out after each
//! draw. mitsuami's `GpuSurface` is presented to from any thread
//! (`SurfaceHandle`).

use player_core::{Gpu, GpuView, MinSize, Session};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;

/// What the UI thread asked for since the thread last looked.
#[derive(Default)]
struct Asks {
    /// QEMU published a frame or changed the cursor
    wake: bool,
    draw: bool,
    /// the surface's new size (physical pixels) and scale
    resize: Option<(u32, u32, f64)>,
    scale: Option<Option<u32>>,
    shot: bool,
    window_shot: bool,
    quit: bool,
    /// asks made, and how many of them the last finished draw had taken
    made: u64,
    done: u64,
}

impl Asks {
    fn any(&self) -> bool {
        self.wake || self.draw || self.resize.is_some() || self.scale.is_some() || self.shot || self.window_shot || self.quit
    }
}

/// What the thread tells the window.
#[derive(Default)]
struct Told {
    view: GpuView,
    /// `Gpu::take_min_size`'s last answer, until the UI thread takes it
    min: Option<MinSize>,
    /// QEMU's main loop returned (`Session::wake`)
    stopped: bool,
}

pub struct Shared {
    asks: Mutex<Asks>,
    asked: Condvar,
    drawn: Condvar,
    told: Mutex<Told>,
    /// the pointer is locked: the guest's cursor goes into the frame
    locked: AtomicBool,
    /// wakes the UI thread (`wake.rs`)
    ui: Arc<dyn Fn() + Send + Sync>,
}

impl Shared {
    pub fn new(ui: Arc<dyn Fn() + Send + Sync>) -> Arc<Shared> {
        Arc::new(Shared {
            asks: Mutex::default(),
            asked: Condvar::new(),
            drawn: Condvar::new(),
            told: Mutex::default(),
            locked: AtomicBool::new(false),
            ui,
        })
    }

    /// The function QEMU's thread calls (`Session::start`'s waker).
    pub fn notifier(self: &Arc<Self>) -> Arc<dyn Fn() + Send + Sync> {
        let shared = self.clone();
        Arc::new(move || {
            shared.ask(|a| a.wake = true);
        })
    }

    fn ask(&self, f: impl FnOnce(&mut Asks)) -> u64 {
        let mut asks = self.asks.lock().unwrap();
        f(&mut asks);
        asks.made += 1;
        self.asked.notify_one();
        asks.made
    }
}

pub struct Render {
    shared: Arc<Shared>,
    thread: Option<JoinHandle<Gpu>>,
}

impl Render {
    /// Draw `session` with `gpu` from now on, on a new thread: on each of
    /// QEMU's wakes, or every `period` for what animates by itself (the
    /// test pattern, the sweep, the calibration).
    pub fn start(shared: Arc<Shared>, gpu: Gpu, session: Arc<Mutex<Session>>, period: Option<Duration>) -> Render {
        let s = shared.clone();
        let thread = std::thread::Builder::new()
            .name("render".into())
            .spawn(move || run(&s, gpu, &session, period))
            .expect("the render thread");
        Render { shared, thread: Some(thread) }
    }

    /// Draw again, with no new frame (a menu changed the picture).
    pub fn draw(&self) {
        self.shared.ask(|a| a.draw = true);
    }

    /// The surface's new size: drawn at it before this returns, never the
    /// old frame stretched, unless the draw takes longer than a few frames.
    pub fn resize(&self, width: u32, height: u32, scale: f64) {
        let mine = self.shared.ask(|a| a.resize = Some((width, height, scale)));
        let asks = self.shared.asks.lock().unwrap();
        let _ = self.shared.drawn.wait_timeout_while(asks, Duration::from_millis(50), |a| a.done < mine);
    }

    pub fn set_fixed_scale(&self, scale: Option<u32>) {
        self.shared.ask(|a| a.scale = Some(scale));
    }

    pub fn screenshot(&self) {
        self.shared.ask(|a| a.shot = true);
    }

    pub fn window_shot(&self) {
        self.shared.ask(|a| a.window_shot = true);
    }

    /// Whether the pointer is locked (the frame then carries the guest's
    /// cursor).
    pub fn set_locked(&self, locked: bool) {
        if self.shared.locked.swap(locked, Ordering::AcqRel) != locked {
            self.draw();
        }
    }

    /// The picture as last drawn.
    pub fn view(&self) -> GpuView {
        self.shared.told.lock().unwrap().view
    }

    /// The window's floor, when it changed since the last call.
    pub fn take_min_size(&self) -> Option<MinSize> {
        self.shared.told.lock().unwrap().min.take()
    }

    /// QEMU's main loop returned: leave, and join it.
    pub fn stopped(&self) -> bool {
        self.shared.told.lock().unwrap().stopped
    }

    /// End the thread, once its draw is done, and hand back the device.
    /// Waited for a second at most: a swapchain's resize can wait for the
    /// UI thread's messages (DXGI), and this is the UI thread. A thread
    /// still drawing then is left, with its device, to the process's exit.
    pub fn stop(&mut self) -> Option<Gpu> {
        self.shared.ask(|a| a.quit = true);
        let thread = self.thread.take()?;
        let deadline = std::time::Instant::now() + Duration::from_secs(1);
        while !thread.is_finished() {
            if std::time::Instant::now() > deadline {
                eprintln!("[render] the render thread did not stop in 1 s; leaving it");
                return None;
            }
            std::thread::sleep(Duration::from_millis(1));
        }
        thread.join().ok()
    }
}

fn run(shared: &Shared, mut gpu: Gpu, session: &Mutex<Session>, period: Option<Duration>) -> Gpu {
    let offscreen = session.lock().unwrap().offscreen();
    let mut stopped = false;
    loop {
        let (asks, taken) = {
            let mut asks = shared.asks.lock().unwrap();
            while !asks.any() {
                match period {
                    Some(p) => {
                        asks = shared.asked.wait_timeout(asks, p).unwrap().0;
                        asks.draw = true;
                    }
                    None => asks = shared.asked.wait(asks).unwrap(),
                }
            }
            let taken = asks.made;
            let (made, done) = (asks.made, asks.done);
            (std::mem::replace(&mut *asks, Asks { made, done, ..Asks::default() }), taken)
        };
        if asks.quit {
            return gpu;
        }
        if let Some((width, height, scale)) = asks.resize {
            gpu.resize(width, height);
            session.lock().unwrap().tell_window_size(&gpu, (width, height), scale);
        }
        if let Some(scale) = asks.scale {
            gpu.set_fixed_scale(scale);
        }
        if asks.wake && !stopped {
            let mut s = session.lock().unwrap();
            // QEMU's main loop returned (guest power-off, `quit`): nothing
            // more is drawn, and the window leaves
            stopped = s.wake(&mut gpu);
            if !stopped {
                s.headless_tick(&mut gpu);
            }
        }
        if asks.shot {
            gpu.screenshot();
        }
        if asks.window_shot {
            gpu.window_shot();
        }
        if !stopped {
            draw(shared, &mut gpu, session, offscreen);
        }
        {
            let mut told = shared.told.lock().unwrap();
            told.view = gpu.view();
            if let Some(min) = gpu.take_min_size() {
                told.min = Some(min);
            }
            told.stopped = stopped;
        }
        shared.asks.lock().unwrap().done = taken;
        shared.drawn.notify_all();
        (shared.ui)();
    }
}

/// One frame: the next image acquired (the wait for the screen), then the
/// newest picture through the chain into it, and presented.
fn draw(shared: &Shared, gpu: &mut Gpu, session: &Mutex<Session>, offscreen: bool) {
    let frame = if offscreen {
        None
    } else {
        match gpu.acquire() {
            Some(f) => Some(f),
            None => return,
        }
    };
    let mut s = session.lock().unwrap();
    // The host pointer sits exactly where the guest's cursor is only with
    // an absolute device (the USB tablet) and no lock; otherwise the
    // guest's cursor is drawn into the frame.
    let sprite = shared.locked.load(Ordering::Acquire) || !s.vm().is_some_and(|v| v.mouse_is_absolute());
    let published = s.draw(gpu, frame.as_ref(), sprite);
    if let Some(frame) = frame {
        gpu.present(frame);
    }
    s.presented(gpu, published);
}
