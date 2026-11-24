// Reuse the established message handling through the loader's shared WndProc
// hook. No private window subclass survives the manager switching a plugin off.
static WNDPROC native_window_proc;
static bool forwarding_window, window_forwarded;
static UINT current_message;
static WPARAM current_wp;
static LPARAM current_lp;
// Canonical addon commands must execute even when identical to the physical
// event. Unlike bridge_forward, this never asks the outer hook to replay it.
static inline LRESULT CALLBACK bridge_direct(HWND window,UINT message,WPARAM wp,LPARAM lp){
    bool previous=forwarding_window;forwarding_window=true;
    LRESULT result=CallWindowProcW(native_window_proc,window,message,wp,lp);
    forwarding_window=previous;return result;
}
static LRESULT CALLBACK bridge_forward(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == current_message && wp == current_wp && lp == current_lp) {
        window_forwarded = true;
        return 0;
    }
    forwarding_window = true;
    LRESULT result = CallWindowProcW(native_window_proc, window, message, wp, lp);
    forwarding_window = false;
    return result;
}
