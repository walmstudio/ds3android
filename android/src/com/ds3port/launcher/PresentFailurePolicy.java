package com.ds3port.launcher;

import java.util.function.Supplier;

final class PresentFailurePolicy {
    private PresentFailurePolicy() { }

    static boolean isLive(Object dialog, Object active, long shownId, boolean activeSurface,
                          Supplier<long[]> readStatus) {
        return dialog != null && dialog == active && activeSurface && matches(shownId, incidentId(readStatus.get()));
    }

    static boolean matches(long shownId, long liveId) {
        return shownId != 0 && shownId == liveId;
    }

    static long incidentId(long[] status) {
        return status != null && status.length > 10 ? status[10] : 0;
    }
}
