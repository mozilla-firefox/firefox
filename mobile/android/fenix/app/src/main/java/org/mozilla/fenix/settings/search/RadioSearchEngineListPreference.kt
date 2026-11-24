/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.search

import android.content.Context
import android.content.res.Resources
import android.util.AttributeSet
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.CompoundButton
import android.widget.LinearLayout
import android.widget.RadioGroup
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.core.graphics.drawable.toDrawable
import androidx.core.view.isVisible
import androidx.navigation.findNavController
import androidx.preference.Preference
import androidx.preference.PreferenceViewHolder
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.state.searchEngines
import mozilla.components.browser.state.state.selectedOrDefaultPrivateSearchEngine
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.ktx.android.view.toScope
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.SearchEngineRadioButtonBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.telemetryName
import org.mozilla.fenix.settings.search.ui.SearchEngineDropdownMenu
import org.mozilla.fenix.theme.FirefoxTheme

class RadioSearchEngineListPreference
@JvmOverloads
constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = android.R.attr.preferenceStyle,
) : Preference(context, attrs, defStyleAttr), CompoundButton.OnCheckedChangeListener {
    private val itemResId: Int
        get() = R.layout.search_engine_radio_button

    private val isForPrivateBrowsing: Boolean
        get() = key == context.getString(R.string.pref_key_private_search_engine_list)

    init {
        layoutResource = R.layout.preference_search_engine_chooser
    }

    override fun onBindViewHolder(holder: PreferenceViewHolder) {
        super.onBindViewHolder(holder)

        subscribeToSearchEngineUpdates(
            context.components.core.store,
            holder.itemView,
        )
    }

    private fun subscribeToSearchEngineUpdates(store: BrowserStore, view: View) =
        view.toScope().launch {
            store
                .flow()
                .map { state -> state.search }
                .distinctUntilChanged()
                .collect { state -> refreshSearchEngineViews(view, state) }
        }

    private fun refreshSearchEngineViews(view: View, state: SearchState) {
        val searchEngineGroup = view.findViewById<RadioGroup>(R.id.search_engine_group)
        searchEngineGroup!!.removeAllViews()
        searchEngineGroup.tag = key

        val layoutInflater = LayoutInflater.from(context)
        val layoutParams =
            ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            )

        val selectedEngine =
            if (isForPrivateBrowsing) {
                state.selectedOrDefaultPrivateSearchEngine
            } else {
                state.selectedOrDefaultSearchEngine
            }

        val hasExplicitPrivateChoice = isForPrivateBrowsing && state.userSelectedPrivateSearchEngineId != null

        if (isForPrivateBrowsing) {
            val useDefaultView =
                makeUseDefaultButton(
                    layoutInflater = layoutInflater,
                    isSelected = !hasExplicitPrivateChoice,
                )
            searchEngineGroup.addView(useDefaultView, layoutParams)
        }

        state.searchEngines
            .filter { engine ->
                engine.type != SearchEngine.Type.APPLICATION
            }
            .forEach { engine ->
                val isSelected =
                    if (isForPrivateBrowsing) {
                        hasExplicitPrivateChoice && engine == selectedEngine
                    } else {
                        engine == selectedEngine
                    }

                val searchEngineView =
                    makeButtonFromSearchEngine(
                        engine = engine,
                        layoutInflater = layoutInflater,
                        res = context.resources,
                        allowDeletion = engine.type == SearchEngine.Type.CUSTOM,
                        isSelected = isSelected,
                    )

                searchEngineGroup.addView(searchEngineView, layoutParams)
            }
    }

    private fun makeUseDefaultButton(
        layoutInflater: LayoutInflater,
        isSelected: Boolean,
    ): View {
        val wrapper = layoutInflater.inflate(itemResId, null) as LinearLayout
        val binding = SearchEngineRadioButtonBinding.bind(wrapper)

        wrapper.setOnClickListener { binding.radioButton.isChecked = true }

        binding.radioButton.tag = USE_DEFAULT_TAG
        binding.radioButton.isChecked = isSelected
        binding.radioButton.setOnCheckedChangeListener(this)
        binding.engineText.text = context.getString(R.string.search_engine_use_default)
        binding.overflowMenu.isVisible = false
        binding.engineIcon.isVisible = false
        return wrapper
    }

    private fun makeButtonFromSearchEngine(
        engine: SearchEngine,
        layoutInflater: LayoutInflater,
        res: Resources,
        allowDeletion: Boolean,
        isSelected: Boolean,
    ): View {
        val isCustomSearchEngine = engine.type == SearchEngine.Type.CUSTOM

        val wrapper = layoutInflater.inflate(itemResId, null) as LinearLayout

        val binding = SearchEngineRadioButtonBinding.bind(wrapper)

        wrapper.setOnClickListener { binding.radioButton.isChecked = true }

        binding.radioButton.tag = engine.id
        binding.radioButton.isChecked = isSelected
        binding.radioButton.setOnCheckedChangeListener(this)
        binding.engineText.text = engine.name

        binding.overflowMenu.isVisible = allowDeletion || isCustomSearchEngine
        if (binding.overflowMenu.isVisible) {
            binding.overflowMenu.setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnDetachedFromWindow)
            binding.overflowMenu.setContent {
                FirefoxTheme {
                    SearchEngineDropdownMenu(
                        isCustomSearchEngine = isCustomSearchEngine,
                        allowDeletion = allowDeletion,
                        onEditClick = { editCustomSearchEngine(wrapper, engine) },
                        onDeleteClick = { deleteSearchEngine(context, engine) },
                    )
                }
            }
        }

        val iconSize = res.getDimension(R.dimen.preference_icon_drawable_size).toInt()
        val engineIcon = engine.icon.toDrawable(res)
        engineIcon.setBounds(0, 0, iconSize, iconSize)
        binding.engineIcon.setImageDrawable(engineIcon)
        return wrapper
    }

    override fun onCheckedChanged(buttonView: CompoundButton, isChecked: Boolean) {
        if (!isChecked) return

        // RadioGroup cannot enforce mutual exclusivity for nested RadioButtons,
        // so manually uncheck all siblings.
        val searchEngineGroup = (buttonView.parent as? View)?.parent as? RadioGroup
        if (searchEngineGroup != null) {
            for (i in 0 until searchEngineGroup.childCount) {
                val child = searchEngineGroup.getChildAt(i) as? ViewGroup ?: continue
                val radioButton = child.findViewById<CompoundButton>(R.id.radio_button) ?: continue
                if (radioButton != buttonView && radioButton.isChecked) {
                    radioButton.isChecked = false
                }
            }
        }

        val searchEngineId = buttonView.tag.toString()

        if (isForPrivateBrowsing && searchEngineId == USE_DEFAULT_TAG) {
            context.components.useCases.searchUseCases.clearPrivateSearchEngine()
            Events.defaultEngineSelected.record(
                Events.DefaultEngineSelectedExtra(
                    engine = "default",
                    isPrivateDefault = true,
                )
            )
            return
        }

        val engine =
            requireNotNull(
                context.components.core.store.state.search.searchEngines.find { searchEngine ->
                    searchEngine.id == searchEngineId
                }
            )

        if (isForPrivateBrowsing) {
            context.components.useCases.searchUseCases.selectPrivateSearchEngine(engine)
        } else {
            context.components.useCases.searchUseCases.selectSearchEngine(engine)
        }

        Events.defaultEngineSelected.record(
            Events.DefaultEngineSelectedExtra(
                engine = engine.telemetryName(),
                isPrivateDefault = isForPrivateBrowsing,
            )
        )
    }

    private fun editCustomSearchEngine(view: View, engine: SearchEngine) {
        val directions =
            DefaultSearchEngineFragmentDirections.actionDefaultEngineFragmentToSaveSearchEngineFragment(engine.id)
        view.findNavController().nav(R.id.defaultEngineFragment, directions)
    }

    private fun deleteSearchEngine(
        context: Context,
        engine: SearchEngine,
    ) {
        val searchState = context.components.core.store.state.search

        val selectedOrDefaultSearchEngine = searchState.selectedOrDefaultSearchEngine
        if (selectedOrDefaultSearchEngine == engine) {
            val nextSearchEngine =
                searchState.searchEngines.firstOrNull {
                    it.id != engine.id && (it.isGeneral || it.type == SearchEngine.Type.CUSTOM)
                }
                    ?: searchState.searchEngines.firstOrNull {
                        it.id != engine.id
                    }

            nextSearchEngine?.let {
                context.components.useCases.searchUseCases.selectSearchEngine(nextSearchEngine)
            }
        }

        val selectedOrDefaultPrivateSearchEngine = searchState.selectedOrDefaultPrivateSearchEngine
        if (selectedOrDefaultPrivateSearchEngine == engine && searchState.userSelectedPrivateSearchEngineId != null) {
            context.components.useCases.searchUseCases.clearPrivateSearchEngine()
        }

        context.components.useCases.searchUseCases.removeSearchEngine(engine)
    }

    companion object {
        private const val USE_DEFAULT_TAG = "use_default_search_engine"
    }
}
