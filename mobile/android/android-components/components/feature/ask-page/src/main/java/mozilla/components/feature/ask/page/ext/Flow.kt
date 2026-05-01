/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page.ext

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.conflate
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.transform
import mozilla.components.ui.richtext.ir.RichDocument
import mozilla.components.ui.richtext.parsing.Parser
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds

internal fun Flow<String>.mapToRichDocument(dispatcher: CoroutineDispatcher): Flow<Pair<String, RichDocument>> {
    val parser = Parser()
    val buffer = StringBuilder()
    return map { buffer.append(it) }
        .sampledMap { Pair(it.toString(), parser.parse(it.toString())) }
        .flowOn(dispatcher)
}

private val PARSE_THROTTLE = 120.milliseconds

private fun <T, R> Flow<T>.sampledMap(period: Duration = PARSE_THROTTLE, transform: (T) -> R): Flow<R> =
    conflate().transform {
        emit(transform(it))
        delay(period)
    }
