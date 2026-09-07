// SPDX-License-Identifier: GPL-3.0-or-later

package org.gnome.sudoku

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import kotlin.math.ceil
import kotlin.math.sqrt

class SudokuBoardOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(200, 240, 240, 240)
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }

    private val backdropPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(180, 20, 90, 120)
        style = Paint.Style.FILL
    }

    private val selectedPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(140, 255, 191, 0)
        style = Paint.Style.FILL
    }

    private val hudPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 36f
    }

    private var moveCount: Int = 0
    private var selected0: Int = -1
    private var selected1: Int = -1
    private var tileCount: Int = 96

    fun updateBoardState(moveCount: Int, selected0: Int, selected1: Int, tileCount: Int) {
        this.moveCount = moveCount
        this.selected0 = selected0
        this.selected1 = selected1
        this.tileCount = if (tileCount > 0) tileCount else 96
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        if (width <= 0 || height <= 0) {
            return
        }

        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), backdropPaint)

        val cols = ceil(sqrt(tileCount.toDouble())).toInt().coerceAtLeast(1)
        val rows = ((tileCount + cols - 1) / cols).coerceAtLeast(1)
        val marginX = width * 0.06f
        val marginY = height * 0.08f
        val boardW = (width - marginX * 2f).coerceAtLeast(1f)
        val boardH = (height - marginY * 2f).coerceAtLeast(1f)
        val cellW = boardW / cols
        val cellH = boardH / rows
        val tileW = cellW * 0.86f
        val tileH = cellH * 0.78f
        val rect = RectF()

        for (row in 0 until rows) {
            for (col in 0 until cols) {
                val idx = row * cols + col
                if (idx >= tileCount) {
                    continue
                }

                val stagger = if (row % 2 == 0) 0f else cellW * 0.5f
                val centerX = marginX + stagger + (col + 0.5f) * cellW
                val centerY = marginY + (row + 0.5f) * cellH
                val left = centerX - tileW * 0.5f
                val top = centerY - tileH * 0.5f
                val right = centerX + tileW * 0.5f
                val bottom = centerY + tileH * 0.5f

                rect.set(left, top, right, bottom)

                if (idx == selected0 || idx == selected1) {
                    canvas.drawRoundRect(rect, 14f, 14f, selectedPaint)
                }

                canvas.drawRoundRect(rect, 14f, 14f, gridPaint)
            }
        }

        canvas.drawText(
            "moves=$moveCount selected=[$selected0,$selected1] tiles=$tileCount",
            24f,
            48f,
            hudPaint
        )
    }
}
