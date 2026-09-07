/*
 * Copyright (c) 2026 Kristjan Esperanto
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

package org.gtk.android;

import android.os.Handler;
import android.os.Looper;
import android.view.Choreographer;

/**
 * ChoreographerHelper drives a GdkAndroidChoreographerSource (GSource) by
 * re-posting a Choreographer.FrameCallback on every vsync until stopped.
 *
 * Choreographer.getInstance() must be called on a Looper thread (the UI
 * thread).  This class uses a Handler backed by the main looper so that
 * start() is safe to call from any thread, including the GTK native thread.
 *
 * nativePtr is an opaque long holding the address of the C-side
 * GdkAndroidChoreographerSource.  It is passed back to nativeOnVsync() so
 * the native code can locate the right source without a global.
 */
public final class ChoreographerHelper implements Choreographer.FrameCallback {
    private volatile boolean running = false;
    private final long nativePtr;
    private final Handler mainHandler;

    public ChoreographerHelper(long nativePtr) {
        this.nativePtr   = nativePtr;
        this.mainHandler = new Handler(Looper.getMainLooper());
    }

    /**
     * Start receiving vsync callbacks.  Safe to call from any thread;
     * the actual Choreographer.postFrameCallback() is dispatched to the UI thread.
     */
    public void start() {
        running = true;
        mainHandler.post(() -> Choreographer.getInstance().postFrameCallback(this));
    }

    /** Stop receiving vsync callbacks.  Safe to call from any thread. */
    public void stop() {
        running = false;
    }

    @Override
    public void doFrame(long frameTimeNanos) {
        if (!running)
            return;
        nativeOnVsync(frameTimeNanos, nativePtr);
        // Re-post for the next vsync.
        Choreographer.getInstance().postFrameCallback(this);
    }

    /**
     * Called on the Android UI thread on every vsync.
     * Implemented in gdkandroidchoreographersource.c via RegisterNatives.
     *
     * @param frameTimeNanos  vsync timestamp in nanoseconds (CLOCK_MONOTONIC)
     * @param nativePtr       opaque pointer to GdkAndroidChoreographerSource
     */
    private static native void nativeOnVsync(long frameTimeNanos, long nativePtr);
}
