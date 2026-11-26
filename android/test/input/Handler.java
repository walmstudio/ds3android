package android.os;

public final class Handler {
    public Handler(Looper ignored) {}
    public boolean post(Runnable r) { return true; }
    public boolean postDelayed(Runnable r, long delay) { return true; }
    public void removeCallbacks(Runnable r) {}
}
