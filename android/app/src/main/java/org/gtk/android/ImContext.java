/*
 * Copyright (c) 2024 Florian "sp1rit" <sp1rit@disroot.org>
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

import android.text.Editable;
import android.text.Selection;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.InputMethodManager;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;

import java.util.logging.Logger;

public final class ImContext {
	public static final class SurroundingRetVal {
		public String text;
		public int cursor_index;
		public int anchor_index;

		private SurroundingRetVal(String text, int cursor_idx, int anchor_idx) {
			this.text = text;
			this.cursor_index = cursor_idx;
			this.anchor_index = anchor_idx;
		}
	}

	private long native_ptr;
	public ImContext(long native_ptr) {
		this.native_ptr = native_ptr;
	}

	public native int getInputType();

	public native SurroundingRetVal getSurrounding();
	public native boolean deleteSurrounding(int offset, int n_chars);

	public native String getPreedit();
	public native void updatePreedit(String preedit, int cursor);

	public native boolean commit(String string);

	@Keep
	private static void reset(View view) {
		InputMethodManager imm = view.getContext().getSystemService(InputMethodManager.class);
		imm.restartInput(view);
	}

	final class ImeConnection extends BaseInputConnection {
		private Logger logger;

		public ImeConnection(@NonNull View target) {
			super(target, true);
			this.logger = Logger.getLogger("IME Connection");
		}

		@Override
		public boolean setComposingText(CharSequence text, int newCursorPosition) {
			logger.info("IME: setComposingText()");
			super.setComposingText(text, newCursorPosition);

			Editable content = getEditable();
			GlibContext.blockForMain(() -> {
				int a = Selection.getSelectionStart(content);
				int b = Selection.getSelectionEnd(content);
				if (a > b)
					b = a;

				ImContext.this.updatePreedit(content.toString(), b);
			});
			return true;
		}

		@Override
		public boolean finishComposingText() {
			logger.info("IME: finishComposingText()");
			super.finishComposingText();

			Editable content = getEditable();
			if (content.length() > 0)
				GlibContext.blockForMain(() -> ImContext.this.commit(content.toString()));
			content.clear();
			return true;
		}

		@Override
		public boolean commitText(CharSequence text, int newCursorPosition) {
			logger.info("IME: commitText(\"" + text + "\", " + newCursorPosition + ")");
			super.commitText(text, newCursorPosition);

			Editable content = getEditable();
			GlibContext.blockForMain(() -> ImContext.this.commit(content.toString()));
			content.clear();
			return true;
		}

		@Override
		public boolean deleteSurroundingText(int leftLength, int rightLength) {
			logger.info("IME: deleteSurroundingText(" + leftLength + ", " + rightLength + ")");

			// The stock Samsung keyboard with 'Auto check spelling' enabled sends leftLength > 1.
			KeyEvent deleteKey = new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL);
			for (int i = 0; i < leftLength; i++) sendKeyEvent(deleteKey);
			return super.deleteSurroundingText(leftLength, rightLength);
		}
	}
}
