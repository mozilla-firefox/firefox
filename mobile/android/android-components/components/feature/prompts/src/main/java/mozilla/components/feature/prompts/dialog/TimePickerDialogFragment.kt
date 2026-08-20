/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package mozilla.components.feature.prompts.dialog

import android.app.DatePickerDialog
import android.app.Dialog
import android.app.TimePickerDialog
import android.content.DialogInterface
import android.content.DialogInterface.BUTTON_NEGATIVE
import android.content.DialogInterface.BUTTON_NEUTRAL
import android.content.DialogInterface.BUTTON_POSITIVE
import android.icu.util.TimeZone
import android.os.Bundle
import android.text.format.DateFormat
import android.view.View
import android.widget.DatePicker
import android.widget.TimePicker
import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import androidx.appcompat.app.AlertDialog
import com.google.android.material.datepicker.CalendarConstraints
import com.google.android.material.datepicker.DateValidatorPointBackward
import com.google.android.material.datepicker.DateValidatorPointForward
import com.google.android.material.datepicker.MaterialDatePicker
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.timepicker.MaterialTimePicker
import com.google.android.material.timepicker.TimeFormat
import java.util.Calendar
import java.util.Date
import mozilla.components.feature.prompts.R
import mozilla.components.feature.prompts.ext.hour
import mozilla.components.feature.prompts.ext.millisecond
import mozilla.components.feature.prompts.ext.minute
import mozilla.components.feature.prompts.ext.second
import mozilla.components.feature.prompts.ext.toCalendar
import mozilla.components.feature.prompts.ext.toLocalDayStartAsUtcMillis
import mozilla.components.feature.prompts.widget.MonthAndYearPicker
import mozilla.components.feature.prompts.widget.TimePrecisionPicker
import mozilla.components.support.utils.TimePicker.shouldShowSecondsPicker
import mozilla.components.support.utils.ext.getSerializableCompat
import mozilla.components.ui.widgets.withCenterAlignedButtons

private const val KEY_INITIAL_DATE = "KEY_INITIAL_DATE"
private const val KEY_MIN_DATE = "KEY_MIN_DATE"
private const val KEY_MAX_DATE = "KEY_MAX_DATE"
private const val KEY_SELECTED_DATE = "KEY_SELECTED_DATE"
private const val KEY_SELECTION_TYPE = "KEY_SELECTION_TYPE"
private const val KEY_STEP_VALUE = "KEY_STEP_VALUE"

/**
 * [DialogFragment][androidx.fragment.app.DialogFragment] implementation to display date picker with a native dialog.
 */
internal class TimePickerDialogFragment :
    PromptDialogFragment(),
    DatePicker.OnDateChangedListener,
    TimePicker.OnTimeChangedListener,
    TimePickerDialog.OnTimeSetListener,
    DatePickerDialog.OnDateSetListener,
    DialogInterface.OnClickListener,
    MonthAndYearPicker.OnDateSetListener,
    TimePrecisionPicker.OnTimeSetListener {
    private val initialDate: Date by lazy {
        safeArguments.getSerializableCompat(KEY_INITIAL_DATE, Date::class.java) as Date
    }
    private val minimumDate: Date? by lazy {
        safeArguments.getSerializableCompat(KEY_MIN_DATE, Date::class.java) as? Date
    }
    private val maximumDate: Date? by lazy {
        safeArguments.getSerializableCompat(KEY_MAX_DATE, Date::class.java) as? Date
    }
    private val selectionType: Int by lazy { safeArguments.getInt(KEY_SELECTION_TYPE) }
    private val stepSize: String? by lazy { safeArguments.getString(KEY_STEP_VALUE) }

    @VisibleForTesting(otherwise = PRIVATE)
    internal var selectedDate: Date
        get() = safeArguments.getSerializableCompat(KEY_SELECTED_DATE, Date::class.java) as Date
        set(value) {
            safeArguments.putSerializable(KEY_SELECTED_DATE, value)
        }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        return when (selectionType) {
            SELECTION_TYPE_DATE,
            SELECTION_TYPE_DATE_AND_TIME -> {
                createMaterialDatePickerDialog(isDateTimePicker = selectionType == SELECTION_TYPE_DATE_AND_TIME)
                Dialog(requireContext())
            }

            SELECTION_TYPE_TIME -> {
                val step = stepSize?.toFloat()
                if (shouldShowSecondsPicker(step) && step != null) {
                    createTimeStepPickerDialog(step)
                } else {
                    createMaterialTimePickerDialog(selectedDate.time)
                    Dialog(requireContext())
                }
            }

            SELECTION_TYPE_MONTH -> createMonthPickerDialog()
            else -> throw IllegalArgumentException("Invalid selection type: $selectionType")
        }
    }

    override fun onStart() {
        super.onStart()
        val alertDialog = dialog
        if (alertDialog is AlertDialog) {
            // We want to call the extension function after the show() call on the dialog,
            // and the DialogFragment does that call during onStart().
            alertDialog.withCenterAlignedButtons()
        }
    }

    private fun createMaterialTimePickerDialog(dateSelection: Long? = null) {
        val calendar = initialDate.toCalendar()
        val is24Hour = DateFormat.is24HourFormat(requireContext())
        val timeFormat = if (is24Hour) TimeFormat.CLOCK_24H else TimeFormat.CLOCK_12H

        val timePicker =
            MaterialTimePicker.Builder()
                .setTimeFormat(timeFormat)
                .setHour(calendar.hour)
                .setMinute(calendar.minute)
                .setTitleText(R.string.mozac_feature_prompts_set_time)
                .build()

        timePicker.addOnPositiveButtonClickListener {
            val finalCalendar =
                (dateSelection?.let {
                    Calendar.getInstance().apply {
                        timeInMillis = it - TimeZone.getDefault().getOffset(it)
                    }
                } ?: selectedDate.toCalendar())

            finalCalendar.set(Calendar.HOUR_OF_DAY, timePicker.hour)
            finalCalendar.set(Calendar.MINUTE, timePicker.minute)
            finalCalendar.set(Calendar.SECOND, 0)
            finalCalendar.set(Calendar.MILLISECOND, 0)
            selectedDate = finalCalendar.time

            feature?.onConfirm(sessionId, promptRequestUID, selectedDate)
            dismiss()
        }
        timePicker.addOnNegativeButtonClickListener {
            feature?.onCancel(sessionId, promptRequestUID)
            dismiss()
        }
        timePicker.addOnCancelListener {
            feature?.onCancel(sessionId, promptRequestUID)
            dismiss()
        }
        timePicker.show(parentFragmentManager, timePicker.toString())
    }

    @VisibleForTesting
    internal fun buildCalendarConstraints(): CalendarConstraints =
        CalendarConstraints.Builder()
            .apply {
                // TODO Bug 2063227 - Date picker ignores the min attribute when the input also specifies max
                minimumDate?.let { setValidator(DateValidatorPointForward.from(it.toLocalDayStartAsUtcMillis())) }
                maximumDate?.let { setValidator(DateValidatorPointBackward.before(it.toLocalDayStartAsUtcMillis())) }
            }
            .build()

    private fun createMaterialDatePickerDialog(isDateTimePicker: Boolean = false) {
        val initialUtcTime = initialDate.time + TimeZone.getDefault().getOffset(initialDate.time)

        val datePicker =
            MaterialDatePicker.Builder.datePicker()
                .setSelection(initialUtcTime)
                .setPositiveButtonText(R.string.mozac_feature_prompts_set_date)
                .setNegativeButtonText(R.string.mozac_feature_prompts_cancel)
                .setCalendarConstraints(buildCalendarConstraints())
                .build()

        datePicker.addOnPositiveButtonClickListener { selection ->
            if (isDateTimePicker) {
                createMaterialTimePickerDialog(selection)
            } else {
                // For the date-only picker, we confirm the selection and dismiss everything.
                val millis = (selection ?: 0L) - TimeZone.getDefault().getOffset(selection ?: 0L)

                selectedDate = Date(millis)
                feature?.onConfirm(sessionId, promptRequestUID, selectedDate)
            }
        }
        datePicker.addOnNegativeButtonClickListener {
            feature?.onCancel(sessionId, promptRequestUID)
        }
        datePicker.addOnCancelListener {
            feature?.onCancel(sessionId, promptRequestUID)
        }
        datePicker.addOnDismissListener {
            // Only dismiss parent if we're not showing time picker
            if (!isDateTimePicker && isAdded) {
                dismissAllowingStateLoss()
            }
        }
        datePicker.show(parentFragmentManager, datePicker.toString())
    }

    private fun createMonthPickerDialog(): AlertDialog {
        val view = inflateDateMonthPicker()
        return buildDialogWithView(view, titleResId = R.string.mozac_feature_prompts_set_month)
    }

    private fun buildDialogWithView(view: View, titleResId: Int? = null): AlertDialog {
        val builder =
            MaterialAlertDialogBuilder(requireContext())
                .setView(view)
                .setPositiveButton(R.string.mozac_feature_prompts_set_date, this)
                .setNegativeButton(R.string.mozac_feature_prompts_cancel, this)
                .setNeutralButton(R.string.mozac_feature_prompts_clear, this)

        titleResId?.let { builder.setTitle(it) }

        return builder.create()
    }

    private fun inflateDateMonthPicker(): View {
        return MonthAndYearPicker(
            context = requireContext(),
            selectedDate = initialDate.toCalendar(),
            maxDate = maximumDate?.toCalendar() ?: MonthAndYearPicker.getDefaultMaxDate(),
            minDate = minimumDate?.toCalendar() ?: MonthAndYearPicker.getDefaultMinDate(),
            dateSetListener = this,
        )
    }

    fun createTimeStepPickerDialog(stepValue: Float): AlertDialog {
        val view =
            TimePrecisionPicker(
                context = requireContext(),
                selectedTime = initialDate.toCalendar(),
                maxTime = maximumDate?.toCalendar() ?: TimePrecisionPicker.getDefaultMaxTime(),
                minTime = minimumDate?.toCalendar() ?: TimePrecisionPicker.getDefaultMinTime(),
                stepValue = stepValue,
                timeSetListener = this,
            )
        return buildDialogWithView(view, titleResId = R.string.mozac_feature_prompts_set_time)
    }

    override fun onClick(dialog: DialogInterface?, which: Int) {
        when (which) {
            BUTTON_POSITIVE -> feature?.onConfirm(sessionId, promptRequestUID, selectedDate)
            BUTTON_NEGATIVE -> feature?.onCancel(sessionId, promptRequestUID)
            BUTTON_NEUTRAL -> feature?.onClear(sessionId, promptRequestUID)
        }
    }

    override fun onTimeChanged(picker: TimePicker?, hourOfDay: Int, minute: Int) {
        val calendar = selectedDate.toCalendar()
        calendar.set(Calendar.HOUR_OF_DAY, hourOfDay)
        calendar.set(Calendar.MINUTE, minute)
        selectedDate = calendar.time
    }

    override fun onTimeSet(view: TimePicker?, hourOfDay: Int, minute: Int) {
        onTimeChanged(view, hourOfDay, minute)
        onClick(null, BUTTON_POSITIVE)
    }

    override fun onDateChanged(view: DatePicker?, year: Int, monthOfYear: Int, dayOfMonth: Int) {
        val calendar = Calendar.getInstance()
        calendar.set(year, monthOfYear, dayOfMonth)
        selectedDate = calendar.time
    }

    override fun onDateSet(picker: MonthAndYearPicker, month: Int, year: Int) {
        onDateChanged(null, year, month, 0)
    }

    override fun onTimeSet(
        picker: TimePrecisionPicker,
        hour: Int,
        minute: Int,
        second: Int,
        millisecond: Int,
    ) {
        val calendar = selectedDate.toCalendar()
        calendar.hour = hour
        calendar.minute = minute
        calendar.second = second
        calendar.millisecond = millisecond
        selectedDate = calendar.time
    }

    override fun onDateSet(view: DatePicker?, year: Int, month: Int, dayOfMonth: Int) {
        onDateChanged(view, year, month, dayOfMonth)
        onClick(null, BUTTON_POSITIVE)
    }

    companion object {
        /**
         * A builder method for creating a [TimePickerDialogFragment]
         *
         * @param sessionId to create the dialog.
         * @param promptRequestUID identifier of the [PromptRequest] for which this dialog is shown.
         * @param shouldDismissOnLoad whether or not the dialog should automatically be dismissed when a new page is
         *   loaded.
         * @param title of the dialog.
         * @param initialDate date that will be selected by default.
         * @param minDate the minimumDate date that will be allowed to be selected.
         * @param maxDate the maximumDate date that will be allowed to be selected.
         * @param selectionType indicate which type of time should be selected, valid values are
         *   ([TimePickerDialogFragment.SELECTION_TYPE_DATE], [TimePickerDialogFragment.SELECTION_TYPE_DATE_AND_TIME],
         *   and [TimePickerDialogFragment.SELECTION_TYPE_TIME])
         * @param stepValue value of time jumped whenever the time is incremented/decremented.
         * @return a new instance of [TimePickerDialogFragment]
         */
        fun newInstance(
            sessionId: String,
            promptRequestUID: String,
            shouldDismissOnLoad: Boolean,
            initialDate: Date,
            minDate: Date?,
            maxDate: Date?,
            selectionType: Int = SELECTION_TYPE_DATE,
            stepValue: String? = null,
        ): TimePickerDialogFragment {
            val fragment = TimePickerDialogFragment()
            val arguments = fragment.arguments ?: Bundle()
            fragment.arguments = arguments
            with(arguments) {
                putString(KEY_SESSION_ID, sessionId)
                putString(KEY_PROMPT_UID, promptRequestUID)
                putBoolean(KEY_SHOULD_DISMISS_ON_LOAD, shouldDismissOnLoad)
                putSerializable(KEY_INITIAL_DATE, initialDate)
                putSerializable(KEY_MIN_DATE, minDate)
                putSerializable(KEY_MAX_DATE, maxDate)
                putString(KEY_STEP_VALUE, stepValue)
                putInt(KEY_SELECTION_TYPE, selectionType)
            }
            fragment.selectedDate = initialDate
            return fragment
        }

        const val SELECTION_TYPE_DATE = 1
        const val SELECTION_TYPE_DATE_AND_TIME = 2
        const val SELECTION_TYPE_TIME = 3
        const val SELECTION_TYPE_MONTH = 4
    }
}
