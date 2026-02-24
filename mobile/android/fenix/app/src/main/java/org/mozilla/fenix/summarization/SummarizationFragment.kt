/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.viewModels
import androidx.fragment.compose.content
import androidx.navigation.fragment.navArgs
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import mozilla.components.concept.llm.Llm
import mozilla.components.feature.summarize.SummarizationSettings
import mozilla.components.feature.summarize.SummarizationUi
import mozilla.components.feature.summarize.fakes.FakeCloudProvider
import mozilla.components.feature.summarize.fakes.FakeLlm
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme
import com.google.android.material.R as materialR

/**
 * Summarization UI entry fragment.
 */
class SummarizationFragment : BottomSheetDialogFragment() {
    private val args by navArgs<SummarizationFragmentArgs>()
    private val storeViewModel: SummarizationStoreViewModel by viewModels {
        SummarizationStoreViewModel.factory(
            initializedFromShake = args.fromShake,
            llmProvider = FakeCloudProvider(
                llm = FakeLlm.testRecipe,
            ),
            settings = SummarizationSettings.inMemory(),
        )
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        super.onCreateDialog(savedInstanceState).apply {
            setOnShowListener {
                val bottomSheet = findViewById<View?>(materialR.id.design_bottom_sheet)
                bottomSheet?.setBackgroundResource(android.R.color.transparent)
            }
        }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = content {
        FirefoxTheme {
            SummarizationUi(
                productName = getString(R.string.app_name),
                store = storeViewModel.store,
            )
        }
    }
}

private val FakeLlm.Companion.testRecipe get() = FakeLlm(
    listOf(
        Llm.Response.Success.ReplyPart(
            """
            **Servings:** 4

            **Total Time:** 30 mins

            **Prep Time:** 5 mins

            **Cook Time:** 25 mins

            ## 🥕 Ingredients
            - 1 tablespoon olive oil
            - 2 cloves garlic, minced
            - 3 cups tomato puree
            - 3 cups water
            - 1 teaspoon salt, or to taste
            - ¼ cup olive oil
            - 2 teaspoons red chili flakes, or to taste
            - 6 ounces dry spaghetti
            - salt to taste
            - 1 tablespoon finely chopped parsley, for garnish

            ## 📋 Instructions
            1. Heat 1 tablespoon olive oil in a pot over medium heat. Add garlic and sizzle until golden and fragrant, about 1 minute. Stir in tomato puree, water, and salt. Bring to a simmer, then reduce to low heat and keep warm.
            2. Pour ¼ cup olive oil into a large non-stick skillet over medium-high heat, season with chili flakes and heat until sizzling, about 1 minute. Add raw spaghetti and toss until well coated with chili oil.
            3. Pour in about 3 cups of tomato broth and, using tongs, move pasta from side to side to evenly distribute. Cook, occasionally moving pasta, until most broth is absorbed or evaporated and spaghetti starts frying in the pan.
            4. Turn spaghetti over with tongs, and evenly arrange in the pan. Keep cooking until pasta starts to brown and lightly chars.
            5. Add about 2 more cups of tomato broth, and repeat the process. Continue cooking until spaghetti is charred to your liking and cooked to desired doneness.
            6. Serve with a drizzle of olive oil and more chili flakes if desired.

            ## ⭐️ Tips
            - For a saucier version, add more broth at the end.
            - Don’t be afraid to char the spaghetti—crispy bits are the best part!
            - Can be served with Parmesan cheese or enjoyed as is.

            ## 🥗 Nutrition
            - Calories: 384
            - Protein: 9g
            - Carbs: 50g
            - Fat: 18g
            """.trimIndent(),
        ),
        Llm.Response.Success.ReplyFinished,
    ),
)
