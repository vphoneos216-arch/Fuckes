package com.invisiblex.loader

import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.TextView

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
class DriverAdapter(
    ctx: Context,
    drivers: List<DriverInfo>,
    selectedConfigValue: String?
) : ArrayAdapter<DriverInfo>(ctx, 0, drivers) {

    private var selectedConfigValue: String = selectedConfigValue ?: ""

    /** Update the currently selected mode (by configValue) and refresh the list. */
    fun setSelected(configValue: String?) {
        this.selectedConfigValue = configValue ?: ""
        notifyDataSetChanged()
    }

    override fun getView(position: Int, convertViewIn: View?, parent: ViewGroup): View {
        val holder: ViewHolder
        val convertView: View = if (convertViewIn == null) {
            val inflated = LayoutInflater.from(context).inflate(R.layout.item_driver, parent, false)
            holder = ViewHolder(inflated)
            inflated.tag = holder
            inflated
        } else {
            holder = convertViewIn.tag as ViewHolder
            convertViewIn
        }

        val info = getItem(position) ?: return convertView

        holder.name.text = info.name
        holder.size.text = info.configValue
        holder.hint.text = info.hint

        // Selected state — whole row is now its own rounded card (bg_driver_row_*)
        // with a left accent stripe, instead of just a dim dot + alpha fade.
        val selected = info.configValue == selectedConfigValue
        convertView.setBackgroundResource(
            if (selected) R.drawable.bg_driver_row_selected else R.drawable.bg_driver_row_idle
        )
        holder.stripe.visibility = if (selected) View.VISIBLE else View.INVISIBLE
        holder.dot.background = context.getDrawable(
            if (selected) R.drawable.dot_status else R.drawable.dot_status_dim
        )
        convertView.alpha = if (selected) 1.0f else 0.9f

        // Permission chips — show/hide each independently
        val perms = info.permissions
        setChip(holder.chipRead, perms.contains(DriverInfo.Permission.READ))
        setChip(holder.chipWrite, perms.contains(DriverInfo.Permission.WRITE))
        setChip(holder.chipExec, perms.contains(DriverInfo.Permission.EXECUTE))
        setChip(holder.chipRoot, perms.contains(DriverInfo.Permission.ROOT))

        return convertView
    }

    private fun setChip(chip: TextView, visible: Boolean) {
        chip.visibility = if (visible) View.VISIBLE else View.GONE
    }

    private class ViewHolder(v: View) {
        val dot: View = v.findViewById(R.id.dot_selected)
        val stripe: View = v.findViewById(R.id.stripe_selected)
        val name: TextView = v.findViewById(R.id.tv_driver_name)
        val hint: TextView = v.findViewById(R.id.tv_driver_info)
        val size: TextView = v.findViewById(R.id.tv_driver_item_size)
        val chipRead: TextView = v.findViewById(R.id.chip_read)
        val chipWrite: TextView = v.findViewById(R.id.chip_write)
        val chipExec: TextView = v.findViewById(R.id.chip_exec)
        val chipRoot: TextView = v.findViewById(R.id.chip_root)
    }
}
