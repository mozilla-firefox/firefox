/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.icons

import android.annotation.SuppressLint
import android.content.ComponentCallbacks2
import android.content.Context
import android.graphics.Bitmap
import android.graphics.drawable.Drawable
import android.widget.ImageView
import androidx.annotation.MainThread
import androidx.annotation.VisibleForTesting
import androidx.annotation.WorkerThread
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import java.lang.ref.WeakReference
import java.util.concurrent.Executors
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.icons.compose.IconLoaderScope
import mozilla.components.browser.icons.compose.IconLoaderState
import mozilla.components.browser.icons.compose.InternalIconLoaderScope
import mozilla.components.browser.icons.decoder.ICOIconDecoder
import mozilla.components.browser.icons.decoder.SvgIconDecoder
import mozilla.components.browser.icons.extension.IconMessageHandler
import mozilla.components.browser.icons.generator.DefaultIconGenerator
import mozilla.components.browser.icons.generator.IconGenerator
import mozilla.components.browser.icons.loader.DataUriIconLoader
import mozilla.components.browser.icons.loader.DefaultMemoryInfoProvider
import mozilla.components.browser.icons.loader.DiskIconLoader
import mozilla.components.browser.icons.loader.HttpIconLoader
import mozilla.components.browser.icons.loader.IconLoader
import mozilla.components.browser.icons.loader.MemoryIconLoader
import mozilla.components.browser.icons.loader.MemoryInfoProvider
import mozilla.components.browser.icons.loader.NonBlockingHttpIconLoader
import mozilla.components.browser.icons.pipeline.IconResourceComparator
import mozilla.components.browser.icons.preparer.DiskIconPreparer
import mozilla.components.browser.icons.preparer.IconPreprarer
import mozilla.components.browser.icons.preparer.MemoryIconPreparer
import mozilla.components.browser.icons.preparer.MerinoManifestIconPreparer
import mozilla.components.browser.icons.processor.DiskIconProcessor
import mozilla.components.browser.icons.processor.IconProcessor
import mozilla.components.browser.icons.processor.MemoryIconProcessor
import mozilla.components.browser.icons.utils.IconDiskCache
import mozilla.components.browser.icons.utils.IconMemoryCache
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.memory.MemoryConsumer
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.fetch.Client
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.service.merino.manifest.MerinoManifestProvider
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.utils.NamedThreadFactory
import mozilla.components.support.images.CancelOnDetach
import mozilla.components.support.images.DesiredSize
import mozilla.components.support.images.decoder.AndroidImageDecoder
import mozilla.components.support.images.decoder.ImageDecoder
import mozilla.components.support.ktx.android.content.pixelSizeFor
import mozilla.components.support.ktx.kotlinx.coroutines.flow.filterChanged

@VisibleForTesting internal const val MAXIMUM_SCALE_FACTOR = 2.0f

private const val EXTENSION_MESSAGING_NAME = "MozacBrowserIcons"

// Number of worker threads we are using internally.
private const val THREADS = 3

internal val sharedMemoryCache = IconMemoryCache()
internal val sharedDiskCache = IconDiskCache()

/**
 * Entry point for loading icons for websites.
 *
 * @param context The application [Context].
 * @param httpClient The [Client] used to fetch icons over the network.
 * @param generator The [IconGenerator] to generate an icon if no icon could be loaded.
 * @param memoryInfoProvider Used to check available memory when deciding whether to cache icons.
 * @param manifestProvider An instance of [MerinoManifestProvider] used to look up website metadata for loading an icon.
 * @param preparers List of [IconPreprarer] instances that enrich an [IconRequest] before loading.
 * @param loaders List of [IconLoader] instances used to load icons.
 * @param decoders List of [ImageDecoder] instances to use when decoding a loaded icon into a [Bitmap].
 * @param processors List of [IconProcessor] instances that run after an icon is loaded.
 * @param jobDispatcher [CoroutineDispatcher] used for icon-loading work.
 */
class BrowserIcons(
    private val context: Context,
    httpClient: Client,
    private val generator: IconGenerator = DefaultIconGenerator(),
    private val memoryInfoProvider: MemoryInfoProvider = DefaultMemoryInfoProvider(context),
    manifestProvider: MerinoManifestProvider = MerinoManifestProvider(context.assets),
    private val preparers: List<IconPreprarer> =
        listOf(
            MerinoManifestIconPreparer(manifestProvider = manifestProvider),
            MemoryIconPreparer(sharedMemoryCache),
            DiskIconPreparer(sharedDiskCache),
        ),
    internal var loaders: List<IconLoader> =
        listOf(
            MemoryIconLoader(sharedMemoryCache),
            DiskIconLoader(sharedDiskCache),
            HttpIconLoader(
                httpClient = httpClient,
                memoryInfoProvider = memoryInfoProvider,
            ),
            DataUriIconLoader(),
        ),
    private val decoders: List<ImageDecoder> =
        listOf(
            AndroidImageDecoder(),
            ICOIconDecoder(),
            SvgIconDecoder(),
        ),
    private val processors: List<IconProcessor> =
        listOf(
            MemoryIconProcessor(sharedMemoryCache),
            DiskIconProcessor(sharedDiskCache),
        ),
    jobDispatcher: CoroutineDispatcher =
        Executors.newFixedThreadPool(
                THREADS,
                NamedThreadFactory("BrowserIcons"),
            )
            .asCoroutineDispatcher(),
    val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : MemoryConsumer {
    private val logger = Logger("BrowserIcons")
    private val maximumSize = context.pixelSizeFor(R.dimen.mozac_browser_icons_maximum_size)
    private val minimumSize = context.pixelSizeFor(R.dimen.mozac_browser_icons_minimum_size)
    private val scope = CoroutineScope(jobDispatcher)
    private val backgroundHttpIconLoader =
        NonBlockingHttpIconLoader(
            httpClient = httpClient,
            memoryInfoProvider = DefaultMemoryInfoProvider(context),
        ) { request, resource, result ->
            val desiredSize = request.getDesiredSize(context, minimumSize, maximumSize)

            val icon = decodeIconLoaderResult(result, decoders, desiredSize) ?: generator.generate(context, request)

            process(context, processors, request, resource, icon, desiredSize)
        }

    /** Asynchronously loads an [Icon] for the given [IconRequest]. */
    fun loadIcon(request: IconRequest): Deferred<Icon> = loadIconInternalAsync(request)

    /** Synchronously loads an [Icon] for the given [IconRequest] using an in-memory loader. */
    private fun loadIconMemoryOnly(initialRequest: IconRequest, desiredSize: DesiredSize): Icon? {
        val preparers = listOf(MemoryIconPreparer(sharedMemoryCache))
        val loaders = listOf(MemoryIconLoader(sharedMemoryCache))
        val request = prepare(context, preparers, initialRequest)

        load(context, request, loaders, decoders, desiredSize)?.let {
            return it.first
        }

        return null
    }

    @WorkerThread
    @VisibleForTesting
    internal fun loadIconInternalAsync(
        initialRequest: IconRequest,
        size: DesiredSize? = null,
    ): Deferred<Icon> = scope.async {
        val desiredSize = size ?: desiredSizeForRequest(initialRequest)

        // (1) First prepare the request.
        val request = prepare(context, preparers, initialRequest)

        // (2) Check whether icons should be downloaded in background.
        val updatedLoaders = loaders.map {
            if (it is HttpIconLoader && !initialRequest.waitOnNetworkLoad) {
                backgroundHttpIconLoader
            } else {
                it
            }
        }

        // (3) Then try to load an icon.
        val (icon, resource) =
            load(context, request, updatedLoaders, decoders, desiredSize)
                ?: (generator.generate(context, request) to null)

        // (4) Finally process the icon.
        val loadedIcon =
            process(context, processors, request, resource, icon, desiredSize) ?: generator.generate(context, request)

        logger.debug("Loaded icon (source = ${loadedIcon.source}): ${request.url}")
        loadedIcon
    }

    /** Installs the "icons" extension in the engine in order to dynamically load icons for loaded websites. */
    fun install(engine: Engine, store: BrowserStore) {
        engine.installBuiltInWebExtension(
            id = "icons@mozac.org",
            url = "resource://android/assets/extensions/browser-icons/",
            onSuccess = { extension ->
                Logger.debug("Installed browser-icons extension")

                store.flowScoped(dispatcher = mainDispatcher) { flow -> subscribeToUpdates(store, flow, extension) }
            },
            onError = { throwable ->
                Logger.error("Could not install browser-icons extension", throwable)
            },
        )
    }

    /**
     * Loads an icon using [BrowserIcons] and then displays it in the [ImageView]. Synchronous loading via an in-memory
     * cache is attempted first, followed by an asynchronous load as a fallback. If the view is detached from the window
     * before loading is completed, then loading is cancelled.
     *
     * @param view [ImageView] to load icon into.
     * @param request Load icon for this given [IconRequest].
     * @param placeholder [Drawable] to display while icon is loading.
     * @param error [Drawable] to display if loading fails.
     */
    fun loadIntoView(
        view: ImageView,
        request: IconRequest,
        placeholder: Drawable? = null,
        error: Drawable? = null,
    ): Job {
        return loadIntoViewInternal(WeakReference(view), request, placeholder, error)
    }

    @MainThread
    @VisibleForTesting
    @Suppress("UndocumentedPublicFunction") // this is visible only for tests
    fun loadIntoViewInternal(
        view: WeakReference<ImageView>,
        request: IconRequest,
        placeholder: Drawable?,
        error: Drawable?,
    ): Job {
        // If we previously started loading into the view, cancel the job.
        val existingJob = view.get()?.getTag(R.id.mozac_browser_icons_tag_job) as? Job
        existingJob?.cancel()

        view.get()?.setImageDrawable(placeholder)

        // Happy path: try to load icon synchronously from an in-memory cache.
        val desiredSize = desiredSizeForRequest(request)
        val inMemoryIcon = loadIconMemoryOnly(request, desiredSize)
        if (inMemoryIcon != null) {
            view.get()?.setImageBitmap(inMemoryIcon.bitmap)
            return Job().also { it.complete() }
        }

        // Unhappy path: if the in-memory load didn't succeed, try the expensive IO loaders.
        @SuppressLint("WrongThread") val deferredIcon = loadIconInternalAsync(request, desiredSize)
        view.get()?.setTag(R.id.mozac_browser_icons_tag_job, deferredIcon)
        val onAttachStateChangeListener =
            CancelOnDetach(deferredIcon).also {
                view.get()?.addOnAttachStateChangeListener(it)
            }

        return scope.launch(Dispatchers.Main) {
            try {
                val icon = deferredIcon.await()
                view.get()?.setImageBitmap(icon.bitmap)
            } catch (e: CancellationException) {
                view.get()?.setImageDrawable(error)
            } finally {
                view.get()?.removeOnAttachStateChangeListener(onAttachStateChangeListener)
                view.get()?.setTag(R.id.mozac_browser_icons_tag_job, null)
            }
        }
    }

    /**
     * Loads an icon using [BrowserIcons] into the given Composable [content]. Synchronous loading via an in-memory
     * cache is attempted first, followed by an asynchronous load as a fallback.
     *
     * @param url The URL of the website an icon should be loaded for.
     * @param iconResource Optional [IconRequest.Resource] to load the icon from.
     * @param iconSize The preferred size of the icon that should be loaded.
     * @param isPrivate Whether this request for this icon came from a private session.
     * @param content The Composable content block to render the icon.
     */
    @Composable
    fun LoadableImage(
        url: String,
        iconResource: IconRequest.Resource? = null,
        iconSize: IconRequest.Size = IconRequest.Size.DEFAULT,
        isPrivate: Boolean = false,
        content: @Composable IconLoaderScope.() -> Unit,
    ) {
        val iconResources = iconResource?.let { listOf(it) } ?: emptyList()
        val request = IconRequest(url, iconSize, iconResources, null, isPrivate)
        val iconLoaderScope = remember(request) { InternalIconLoaderScope() }

        // Happy path: try to load icon synchronously from an in-memory cache.
        val desiredSize = desiredSizeForRequest(request)
        val inMemoryIcon = loadIconMemoryOnly(request, desiredSize)
        if (inMemoryIcon != null) {
            iconLoaderScope.state.value =
                IconLoaderState.Icon(
                    BitmapPainter(inMemoryIcon.bitmap.asImageBitmap()),
                    inMemoryIcon.color,
                    inMemoryIcon.source,
                    inMemoryIcon.maskable,
                )
        } else {
            // Unhappy path: if the in-memory load didn't succeed, try the expensive IO loaders.
            val deferredIcon = loadIconInternalAsync(request, desiredSize)

            LaunchedEffect(request) {
                try {
                    val icon = deferredIcon.await()
                    iconLoaderScope.state.value =
                        IconLoaderState.Icon(
                            BitmapPainter(icon.bitmap.asImageBitmap()),
                            icon.color,
                            icon.source,
                            icon.maskable,
                        )
                } catch (e: CancellationException) {
                    Logger.debug("Could not retrieve icon for $url", e)
                }
            }
        }

        iconLoaderScope.content()
    }

    private fun desiredSizeForRequest(request: IconRequest) =
        DesiredSize(
            targetSize = context.pixelSizeFor(request.size.dimen),
            minSize = minimumSize,
            maxSize = maximumSize,
            maxScaleFactor = MAXIMUM_SCALE_FACTOR,
        )

    /** The device is running low on memory. This component should trim its memory usage. */
    @Deprecated("Use onTrimMemory instead.", replaceWith = ReplaceWith("onTrimMemory"))
    fun onLowMemory() {
        sharedMemoryCache.clear()
    }

    @Suppress("DEPRECATION") // Apps are not notified of these levels since API level 34.
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=1909473
    override fun onTrimMemory(level: Int) {
        val shouldClearMemoryCache =
            when (level) {
                // Foreground: The device is running much lower on memory. The app is running and not killable, but the
                // system wants us to release unused resources to improve system performance.
                ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW,
                // Foreground: The device is running extremely low on memory. The app is not yet considered a killable
                // process, but the system will begin killing background processes if apps do not release resources.
                ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL -> true

                // Background: The system is running low on memory and our process is near the middle of the LRU list.
                // If the system becomes further constrained for memory, there's a chance our process will be killed.
                ComponentCallbacks2.TRIM_MEMORY_MODERATE,
                // Background: The system is running low on memory and our process is one of the first to be killed
                // if the system does not recover memory now.
                ComponentCallbacks2.TRIM_MEMORY_COMPLETE -> true

                else -> false
            }

        if (shouldClearMemoryCache) {
            sharedMemoryCache.clear()
        }
    }

    /**
     * Clears all icons and metadata from disk and memory.
     *
     * This will clear the default disk and memory cache that is used by the default configuration. If custom
     * [IconLoader] and [IconProcessor] instances with a custom storage are provided to [BrowserIcons] then the calling
     * app is responsible for clearing that data.
     */
    fun clear() {
        sharedDiskCache.clear(context)
        sharedMemoryCache.clear()
    }

    private suspend fun subscribeToUpdates(
        store: BrowserStore,
        flow: Flow<BrowserState>,
        extension: WebExtension,
    ) {
        // Whenever we see a new EngineSession in the store then we register our content message
        // handler if it has not been added yet.

        flow
            .map { it.tabs }
            .filterChanged { it.engineState.engineSession }
            .collect { state ->
                val engineSession = state.engineState.engineSession ?: return@collect

                if (extension.hasContentMessageHandler(engineSession, EXTENSION_MESSAGING_NAME)) {
                    return@collect
                }

                val handler = IconMessageHandler(store, state.id, state.content.private, this)
                extension.registerContentMessageHandler(engineSession, EXTENSION_MESSAGING_NAME, handler)
            }
    }
}

private fun prepare(context: Context, preparers: List<IconPreprarer>, request: IconRequest): IconRequest =
    preparers.fold(request) { preparedRequest, preparer ->
        preparer.prepare(context, preparedRequest)
    }

private fun load(
    context: Context,
    request: IconRequest,
    loaders: List<IconLoader>,
    decoders: List<ImageDecoder>,
    desiredSize: DesiredSize,
): Pair<Icon, IconRequest.Resource>? {
    request.resources
        .asSequence()
        .sortedWith(IconResourceComparator)
        .distinctBy { it.url }
        .forEach { resource ->
            loaders.forEach { loader ->
                val result = loader.load(context, request, resource)

                val icon = decodeIconLoaderResult(result, decoders, desiredSize)

                if (icon != null) {
                    return Pair(icon, resource)
                }
            }
        }

    return null
}

private fun decodeIconLoaderResult(
    result: IconLoader.Result,
    decoders: List<ImageDecoder>,
    desiredSize: DesiredSize,
): Icon? =
    when (result) {
        IconLoader.Result.NoResult -> null

        is IconLoader.Result.BitmapResult -> Icon(result.bitmap, source = result.source)

        is IconLoader.Result.BytesResult ->
            decodeBytes(result.bytes, decoders, desiredSize)?.let { Icon(it, source = result.source) }
    }

@VisibleForTesting
internal fun IconRequest.getDesiredSize(context: Context, minimumSize: Int, maximumSize: Int) =
    DesiredSize(
        targetSize = context.pixelSizeFor(size.dimen),
        minSize = minimumSize,
        maxSize = maximumSize,
        maxScaleFactor = MAXIMUM_SCALE_FACTOR,
    )

private fun decodeBytes(
    data: ByteArray,
    decoders: List<ImageDecoder>,
    desiredSize: DesiredSize,
): Bitmap? {
    decoders.forEach { decoder ->
        val bitmap = decoder.decode(data, desiredSize)

        if (bitmap != null) {
            return bitmap
        }
    }

    return null
}

private fun process(
    context: Context,
    processors: List<IconProcessor>,
    request: IconRequest,
    resource: IconRequest.Resource?,
    icon: Icon?,
    desiredSize: DesiredSize,
): Icon? =
    processors.fold(icon) { processedIcon, processor ->
        if (processedIcon == null) return null
        processor.process(context, request, resource, processedIcon, desiredSize)
    }
