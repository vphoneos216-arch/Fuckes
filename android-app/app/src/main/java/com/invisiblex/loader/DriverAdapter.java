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
 *
 * Each row shows:
 *   • backend mode name (monospace)
 *   • one-line description of how that backend actually works
 *   • permission chips: READ · WRITE · EXEC · ROOT (colored, shown per mode)
 *   • config value that gets written to config.json's "driver" key
 *   • selection dot (dim → lit on selection)
 *
 * Selection is keyed by DriverInfo.configValue (e.g. "auto", "ditpro"),
 * not by display name — that's the exact value DriverConfig writes and
 * the exact value ix_driver_number() in ix_driver_init.h parses back.
 */
public class DriverAdapter extends ArrayAdapter<DriverInfo> {

    private String selectedConfigValue;

    public DriverAdapter(@NonNull Context ctx,
                         @NonNull List<DriverInfo> drivers,
                         String selectedConfigValue) {
        super(ctx, 0, drivers);
        this.selectedConfigValue = selectedConfigValue != null ? selectedConfigValue : "";
    }

    /** Update the currently selected mode (by configValue) and refresh the list. */
    public void setSelected(String configValue) {
        this.selectedConfigValue = configValue != null ? configValue : "";
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
        holder.size.setText(info.configValue);
        holder.hint.setText(info.hint);

        // Selected state — whole row is now its own rounded card (bg_driver_row_*)
        // with a left accent stripe, instead of just a dim dot + alpha fade.
        boolean selected = info.configValue.equals(selectedConfigValue);
        convertView.setBackgroundResource(
            selected ? R.drawable.bg_driver_row_selected : R.drawable.bg_driver_row_idle);
        holder.stripe.setVisibility(selected ? View.VISIBLE : View.INVISIBLE);
        holder.dot.setBackground(getContext().getDrawable(
            selected ? R.drawable.dot_status : R.drawable.dot_status_dim));
        convertView.setAlpha(selected ? 1.0f : 0.9f);

        // Permission chips — show/hide each independently
        List<DriverInfo.Permission> perms = info.permissions;
        setChip(holder.chipRead,  perms.contains(DriverInfo.Permission.READ));
        setChip(holder.chipWrite, perms.contains(DriverInfo.Permission.WRITE));
        setChip(holder.chipExec,  perms.contains(DriverInfo.Permission.EXECUTE));
        setChip(holder.chipRoot,  perms.contains(DriverInfo.Permission.ROOT));

        return convertView;
    }

    private static void setChip(TextView chip, boolean visible) {
        chip.setVisibility(visible ? View.VISIBLE : View.GONE);
    }

    static class ViewHolder {
        final View     dot;
        final View     stripe;
        final TextView name;
        final TextView hint;
        final TextView size;
        final TextView chipRead;
        final TextView chipWrite;
        final TextView chipExec;
        final TextView chipRoot;

        ViewHolder(View v) {
            dot       = v.findViewById(R.id.dot_selected);
            stripe    = v.findViewById(R.id.stripe_selected);
            name      = v.findViewById(R.id.tv_driver_name);
            hint      = v.findViewById(R.id.tv_driver_info);
            size      = v.findViewById(R.id.tv_driver_item_size);
            chipRead  = v.findViewById(R.id.chip_read);
            chipWrite = v.findViewById(R.id.chip_write);
            chipExec  = v.findViewById(R.id.chip_exec);
            chipRoot  = v.findViewById(R.id.chip_root);
        }
    }
}
