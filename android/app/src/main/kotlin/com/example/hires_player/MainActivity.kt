package com.example.hires_player

import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {

    private val CHANNEL = "native_audio"

    companion object {
        init {
            System.loadLibrary("native_audio")
        }
    }

    private external fun nativeGetEngineStatus(): String

    private external fun nativePlayTestTone(): String

    private external fun nativeStopAudio(): String


    override fun configureFlutterEngine(
        flutterEngine: FlutterEngine
    ) {

        super.configureFlutterEngine(flutterEngine)


        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            CHANNEL
        ).setMethodCallHandler { call, result ->

            when (call.method) {

                "getEngineStatus" -> {

                    try {

                        val status =
                            nativeGetEngineStatus()

                        result.success(status)

                    } catch (e: Exception) {

                        result.error(
                            "NATIVE_ERROR",
                            e.message,
                            null
                        )
                    }
                }


                "playTestTone" -> {

                    try {

                        val status =
                            nativePlayTestTone()

                        result.success(status)

                    } catch (e: Exception) {

                        result.error(
                            "AUDIO_ERROR",
                            e.message,
                            null
                        )
                    }
                }


                "stopAudio" -> {

                    try {

                        val status =
                            nativeStopAudio()

                        result.success(status)

                    } catch (e: Exception) {

                        result.error(
                            "AUDIO_ERROR",
                            e.message,
                            null
                        )
                    }
                }


                else -> {
                    result.notImplemented()
                }
            }
        }
    }
}