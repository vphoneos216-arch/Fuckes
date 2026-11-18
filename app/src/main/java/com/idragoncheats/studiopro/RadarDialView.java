package com.idragoncheats.studiopro;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

/**
 * Purely decorative "launch control" dial: concentric rings, tick marks and
 * two accent arcs, matching the reference UI's center visual. No game data,
 * no hooked behaviour — just drawn shapes.
 */
public class RadarDialView extends View {

    private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint tickPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint crossPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint arcPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint dotPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    private float pulsePhase = 0f;

    public RadarDialView(Context context) {
        super(context);
        init();
    }

    public RadarDialView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        ringPaint.setStyle(Paint.Style.STROKE);
        ringPaint.setStrokeWidth(dp(1f));
        ringPaint.setColor(Color.parseColor("#223156"));

        tickPaint.setStyle(Paint.Style.STROKE);
        tickPaint.setStrokeWidth(dp(1.5f));
        tickPaint.setColor(Color.parseColor("#2A3B5F"));

        crossPaint.setStyle(Paint.Style.STROKE);
        crossPaint.setStrokeWidth(dp(1f));
        crossPaint.setColor(Color.parseColor("#1A2642"));

        arcPaint.setStyle(Paint.Style.STROKE);
        arcPaint.setStrokeWidth(dp(3f));
        arcPaint.setStrokeCap(Paint.Cap.ROUND);
        arcPaint.setColor(Color.parseColor("#49C9F0"));

        dotPaint.setStyle(Paint.Style.FILL);
        dotPaint.setColor(Color.parseColor("#49C9F0"));
    }

    private float dp(float v) {
        return v * getResources().getDisplayMetrics().density;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        float cx = getWidth() / 2f;
        float cy = getHeight() / 2f;
        float maxR = Math.min(getWidth(), getHeight()) / 2f - dp(16f);
        if (maxR <= 0) return;

        // crosshair lines
        canvas.drawLine(cx - maxR - dp(20f), cy, cx + maxR + dp(20f), cy, crossPaint);
        canvas.drawLine(cx, cy - maxR - dp(20f), cx, cy + maxR + dp(20f), crossPaint);

        // three concentric rings
        for (int i = 1; i <= 3; i++) {
            float r = maxR * i / 3f;
            canvas.drawCircle(cx, cy, r, ringPaint);
        }

        // tick marks every 15 degrees around the outer ring
        float outerR = maxR;
        float tickInner = outerR - dp(6f);
        for (int deg = 0; deg < 360; deg += 15) {
            double rad = Math.toRadians(deg);
            float x1 = cx + (float) (Math.cos(rad) * outerR);
            float y1 = cy + (float) (Math.sin(rad) * outerR);
            float x2 = cx + (float) (Math.cos(rad) * tickInner);
            float y2 = cy + (float) (Math.sin(rad) * tickInner);
            canvas.drawLine(x1, y1, x2, y2, tickPaint);
        }

        // two accent arcs on the outer ring (cosmetic, static angles)
        RectF oval = new RectF(cx - outerR, cy - outerR, cx + outerR, cy + outerR);
        canvas.drawArc(oval, -75, 55, false, arcPaint);
        canvas.drawArc(oval, 150, 45, false, arcPaint);

        // small accent dots on the middle ring, opposite corners
        float midR = maxR * 2f / 3f;
        double d1 = Math.toRadians(-20);
        double d2 = Math.toRadians(160);
        canvas.drawCircle(cx + (float) (Math.cos(d1) * midR), cy + (float) (Math.sin(d1) * midR), dp(3.5f), dotPaint);
        canvas.drawCircle(cx + (float) (Math.cos(d2) * midR), cy + (float) (Math.sin(d2) * midR), dp(3.5f), dotPaint);
    }
}
