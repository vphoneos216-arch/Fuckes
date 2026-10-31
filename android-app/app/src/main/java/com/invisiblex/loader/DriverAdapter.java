package com.invisiblex.loader;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.List;

/**
 * DriverAdapter — ListView adapter for the driver selection list.
 * Each row shows driver name, size, and a selection indicator dot.
 */
public class DriverAdapter extends ArrayAdapter<DriverInfo> {

    private String selectedDriver;

    public DriverAdapter(@NonNull Context ctx,
                         @NonNull List<DriverInfo> drivers,
                         String selectedDriver) {
        super(ctx, 0, drivers);
        this.selectedDriver = selectedDriver != null ? selectedDriver : "";
    }

    /** Update the currently selected driver and refresh the list. */
    public void setSelected(String name) {
        this.selectedDriver = name != null ? name : "";
        notifyDataSetChanged();
    }

    @NonNull
    @Override
    public View getView(int position, @Nullable View convertView, @NonNull ViewGroup parent) {
        ViewHolder holder;
        if (convertView == null) {
            convertView = LayoutInflater.from(getContext())
                .inflate(R.layout.item_driver, parent, false);
            holder = new ViewHolder(convertView);
            convertView.setTag(holder);
        } else {
            holder = (ViewHolder) convertView.getTag();
        }

        DriverInfo info = getItem(position);
        if (info == null) return convertView;

        holder.name.setText(info.name);
        holder.size.setText(info.sizeLabel);

        // Kernel hint — e.g. "5.10", "5.15", "6.1", or "Universal"
        String hint = guessKernelHint(info.name);
        holder.info.setText(hint);

        // Selection indicator — dot_selected is always VISIBLE; background swaps
        boolean selected = info.name.equals(selectedDriver);
        holder.dot.setVisibility(View.VISIBLE);
        holder.dot.setBackground(getContext().getDrawable(
            selected ? R.drawable.dot_status : R.drawable.dot_status_dim));
        convertView.setAlpha(selected ? 1.0f : 0.75f);

        return convertView;
    }

    private String guessKernelHint(String filename) {
        String lower = filename.toLowerCase();
        if (lower.contains("4.4"))  return "Kernel 4.4";
        if (lower.contains("4.9"))  return "Kernel 4.9";
        if (lower.contains("4.14")) return "Kernel 4.14";
        if (lower.contains("4.19")) return "Kernel 4.19";
        if (lower.contains("5.4"))  return "Kernel 5.4";
        if (lower.contains("5.10") || lower.contains("5_10")) return "Kernel 5.10";
        if (lower.contains("5.15") || lower.contains("5_15")) return "Kernel 5.15";
        if (lower.contains("6.1")  || lower.contains("6_1"))  return "Kernel 6.1";
        if (lower.contains("6.6")  || lower.contains("6_6"))  return "Kernel 6.6";
        if (lower.contains("ksu") || lower.contains("kernel-su")) return "KernelSU";
        if (lower.contains("magisk")) return "Magisk";
        if (lower.contains("apatch")) return "APatch";
        return "Select if compatible";
    }

    static class ViewHolder {
        final View     dot;
        final TextView name;
        final TextView info;
        final TextView size;

        ViewHolder(View v) {
            dot  = v.findViewById(R.id.dot_selected);
            name = v.findViewById(R.id.tv_driver_name);
            info = v.findViewById(R.id.tv_driver_info);
            size = v.findViewById(R.id.tv_driver_item_size);
        }
    }
}
