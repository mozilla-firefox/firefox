package org.mozilla.geckoview.test.util

import android.content.Context
import android.webkit.MimeTypeMap
import java.io.FileNotFoundException
import java.net.InetSocketAddress
import java.util.Locale
import java.util.concurrent.TimeUnit
import kotlin.random.Random
import mockwebserver3.Dispatcher
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import mockwebserver3.RecordedRequest
import okio.Buffer
import org.json.JSONObject

class TestServer
@JvmOverloads
constructor(
    context: Context,
    private val customHeaders: Map<String, String>? = null,
    private val responseModifiers: Map<String, ResponseModifier>? = null,
) {
    private val assets = context.resources.assets
    private val server =
        MockWebServer().apply {
            dispatcher =
                object : Dispatcher() {
                    override fun dispatch(request: RecordedRequest) = handle(request)
                }
        }

    private fun handle(request: RecordedRequest): MockResponse {
        val path = request.url.encodedPath
        return when {
            path == "/anything" -> anything(request)
            path.startsWith("/assets/") -> asset(request, path)
            path.startsWith("/status/") -> status(path.removePrefix("/status/").toInt())
            path.startsWith("/redirect-to") -> redirect(request.url.queryParameter("url")!!)
            path.startsWith("/redirect/") -> {
                val count = path.substringAfterLast('/').toInt() - 1
                if (count > 0) redirect("/redirect/$count") else status(200)
            }
            path.startsWith("/basic-auth/") ->
                MockResponse.Builder().code(401).setHeader("WWW-Authenticate", "Basic realm=\"Fake Realm\"").build()
            path == "/cookies" -> {
                val cookiesObj = JSONObject()
                request.headers["cookie"]?.split(";")?.forEach {
                    val (name, value) = it.trim().split('=')
                    cookiesObj.put(name, value)
                }
                json(JSONObject().put("cookies", cookiesObj))
            }
            path.startsWith("/cookies/set/") -> {
                val parts = path.removePrefix("/cookies/set/").split('/')
                MockResponse.Builder()
                    .code(302)
                    .setHeader("Set-Cookie", "${parts[0]}=${parts[1]}; Path=/")
                    .setHeader("Location", "/cookies")
                    .build()
            }
            path.startsWith("/bytes/") -> {
                val body = Buffer().write(Random.nextBytes(path.substringAfterLast('/').toInt()))
                MockResponse.Builder()
                    .setHeader("Content-Type", "application/octet-stream")
                    .setHeader("X-SHA-256", body.sha256().hex())
                    .body(body)
                    .build()
            }
            path.startsWith("/trickle/") ->
                MockResponse.Builder()
                    .setHeader("Content-Type", "application/octet-stream")
                    .body(Buffer().write(ByteArray(path.substringAfterLast('/').toInt()) { 1 }))
                    .throttleBody(1, 250, TimeUnit.MILLISECONDS)
                    .build()
            path.startsWith("/stall/") ->
                MockResponse.Builder()
                    .setHeader("Content-Type", "InstallException")
                    .body(Buffer().write(ByteArray(99) { 1 }))
                    .setHeader("Content-Length", 100)
                    .build()
            else -> status(404)
        }
    }

    private fun anything(request: RecordedRequest): MockResponse {
        val headers = JSONObject()
        for (name in request.headers.names()) {
            headers.put(name, request.headers.values(name).joinToString(", "))
        }

        val obj = JSONObject().put("method", request.method).put("headers", headers)
        if (request.method == "POST") {
            obj.put("data", request.body?.utf8())
        }
        return json(obj)
    }

    private fun asset(request: RecordedRequest, path: String): MockResponse {
        val fileName = path.removePrefix("/assets/")
        val asset =
            try {
                assets.open(fileName).use { it.readBytes() }
            } catch (e: FileNotFoundException) {
                return status(404)
            }

        val mimeType =
            when (val ext = fileName.substringAfterLast('.', "").lowercase(Locale.ROOT)) {
                "webmanifest" -> "application/manifest+json"
                else -> MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
            }

        val response = MockResponse.Builder()
        customHeaders?.forEach { (header, value) -> response.setHeader(header, value) }
        response.setHeader("Content-Type", mimeType)
        request.url.queryParameter("contentDisposition")?.let {
            response.setHeader("Content-Disposition", it)
        }

        val bytes =
            responseModifiers?.get(path)?.transformResponse(asset.decodeToString())?.encodeToByteArray() ?: asset
        return response.body(Buffer().write(bytes)).build()
    }

    private fun status(code: Int) = MockResponse.Builder().code(code).build()

    private fun redirect(location: String) = MockResponse.Builder().code(302).setHeader("Location", location).build()

    private fun json(obj: JSONObject) =
        MockResponse.Builder().setHeader("Content-Type", "application/json; charset=utf-8").body(obj.toString()).build()

    fun start(port: Int) = server.start(InetSocketAddress(port).address, port)

    fun stop() = server.close()

    fun interface ResponseModifier {
        fun transformResponse(response: String): String
    }
}
