import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

void main() {
  runApp(const HiResPlayerApp());
}


// ============================================================
// APP
// ============================================================

class HiResPlayerApp extends StatelessWidget {
  const HiResPlayerApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,

      title: 'Hi-Res Player',

      theme: ThemeData(
        brightness: Brightness.dark,
        useMaterial3: true,
      ),

      home: const HomePage(),
    );
  }
}


// ============================================================
// HOME PAGE
// ============================================================

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() {
    return _HomePageState();
  }
}


// ============================================================
// HOME PAGE STATE
// ============================================================

class _HomePageState extends State<HomePage> {

  // ----------------------------------------------------------
  // MethodChannel
  // ----------------------------------------------------------

  static const MethodChannel _channel =
      MethodChannel('native_audio');


  // ----------------------------------------------------------
  // Status
  // ----------------------------------------------------------

  String _status =
      'Audio Engine belum dites';


  bool _isPlaying = false;


  // ==========================================================
  // TEST NATIVE ENGINE
  // ==========================================================

  Future<void> _testEngine() async {

    try {

      setState(() {
        _status = 'Menghubungkan ke Native Audio Engine...';
      });


      final result =
          await _channel.invokeMethod<String>(
        'getEngineStatus',
      );


      setState(() {

        _status =
            result ?? 'Tidak ada response';

      });

    } on PlatformException catch (e) {

      setState(() {

        _status =
            'Engine Error: ${e.message}';

      });

    } catch (e) {

      setState(() {

        _status =
            'Error: $e';

      });
    }
  }


  // ==========================================================
  // MENYIAPKAN FILE WAV
  // ==========================================================

  Future<String> _prepareWavFile() async {

    // --------------------------------------------------------
    // Membaca file dari Flutter Asset
    // --------------------------------------------------------

    final ByteData data =
        await rootBundle.load(
      'assets/audio/test.wav',
    );


    // --------------------------------------------------------
    // Mendapatkan folder aplikasi
    // --------------------------------------------------------

    final Directory directory =
        await getApplicationDocumentsDirectory();


    // --------------------------------------------------------
    // Membuat file test.wav
    // --------------------------------------------------------

    final File file =
        File(
      '${directory.path}/test.wav',
    );


    // --------------------------------------------------------
    // Menulis asset menjadi file fisik
    // --------------------------------------------------------

    await file.writeAsBytes(
      data.buffer.asUint8List(
        data.offsetInBytes,
        data.lengthInBytes,
      ),
      flush: true,
    );


    return file.path;
  }


  // ==========================================================
  // PLAY WAV
  // ==========================================================

  Future<void> _playWav() async {

    try {

      setState(() {

        _status =
            'Menyiapkan file WAV...';

        _isPlaying = false;

      });


      // ------------------------------------------------------
      // Copy asset ke penyimpanan aplikasi
      // ------------------------------------------------------

      final String path =
          await _prepareWavFile();


      setState(() {

        _status =
            'Mengirim WAV ke Native Audio Engine...';

      });


      // ------------------------------------------------------
      // Kirim path ke Kotlin
      // ------------------------------------------------------

      final String? result =
          await _channel.invokeMethod<String>(
        'playWav',

        {
          'path': path,
        },
      );


      // ------------------------------------------------------
      // Update UI
      // ------------------------------------------------------

      setState(() {

        _status =
            result ?? 'WAV sedang diputar';

        _isPlaying = true;

      });

    } on PlatformException catch (e) {

      setState(() {

        _status =
            'WAV Error: ${e.message}';

        _isPlaying = false;

      });

    } catch (e) {

      setState(() {

        _status =
            'Error: $e';

        _isPlaying = false;

      });
    }
  }


  // ==========================================================
  // STOP AUDIO
  // ==========================================================

  Future<void> _stopAudio() async {

    try {

      setState(() {

        _status =
            'Menghentikan audio...';

      });


      final String? result =
          await _channel.invokeMethod<String>(
        'stopAudio',
      );


      setState(() {

        _status =
            result ?? 'Audio berhenti';

        _isPlaying = false;

      });

    } on PlatformException catch (e) {

      setState(() {

        _status =
            'Stop Error: ${e.message}';

      });
    } catch (e) {

      setState(() {

        _status =
            'Error: $e';

      });
    }
  }


  // ==========================================================
  // UI
  // ==========================================================

  @override
  Widget build(BuildContext context) {

    return Scaffold(

      // ------------------------------------------------------
      // APP BAR
      // ------------------------------------------------------

      appBar: AppBar(

        title: const Text(
          'Hi-Res Player',
        ),

        centerTitle: true,
      ),


      // ------------------------------------------------------
      // BODY
      // ------------------------------------------------------

      body: SafeArea(

        child: Center(

          child: SingleChildScrollView(

            padding:
                const EdgeInsets.all(24),


            child: Column(

              mainAxisAlignment:
                  MainAxisAlignment.center,


              children: [

                // ==========================================
                // ICON
                // ==========================================

                Icon(
                  _isPlaying
                      ? Icons.graphic_eq
                      : Icons.headphones,

                  size: 90,
                ),


                const SizedBox(
                  height: 24,
                ),


                // ==========================================
                // TITLE
                // ==========================================

                const Text(
                  'Native Audio Engine',

                  style: TextStyle(
                    fontSize: 26,
                    fontWeight:
                        FontWeight.bold,
                  ),
                ),


                const SizedBox(
                  height: 12,
                ),


                // ==========================================
                // DESCRIPTION
                // ==========================================

                const Text(
                  'Flutter → Kotlin → JNI → C++ → AAudio',

                  textAlign:
                      TextAlign.center,

                  style: TextStyle(
                    fontSize: 14,
                  ),
                ),


                const SizedBox(
                  height: 24,
                ),


                // ==========================================
                // STATUS CARD
                // ==========================================

                Card(

                  child: Padding(

                    padding:
                        const EdgeInsets.all(20),

                    child: Column(

                      children: [

                        const Text(
                          'STATUS',

                          style: TextStyle(
                            fontSize: 12,
                            fontWeight:
                                FontWeight.bold,
                          ),
                        ),


                        const SizedBox(
                          height: 10,
                        ),


                        Text(
                          _status,

                          textAlign:
                              TextAlign.center,

                          style: const TextStyle(
                            fontSize: 17,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),


                const SizedBox(
                  height: 30,
                ),


                // ==========================================
                // TEST ENGINE
                // ==========================================

                SizedBox(

                  width:
                      double.infinity,

                  child:
                      ElevatedButton.icon(

                    onPressed:
                        _testEngine,

                    icon:
                        const Icon(
                      Icons.settings,
                    ),

                    label:
                        const Text(
                      'TEST NATIVE ENGINE',
                    ),
                  ),
                ),


                const SizedBox(
                  height: 12,
                ),


                // ==========================================
                // PLAY WAV
                // ==========================================

                SizedBox(

                  width:
                      double.infinity,

                  child:
                      ElevatedButton.icon(

                    onPressed:
                        _isPlaying
                            ? null
                            : _playWav,

                    icon:
                        const Icon(
                      Icons.play_arrow,
                    ),

                    label:
                        const Text(
                      'PLAY WAV',
                    ),
                  ),
                ),


                const SizedBox(
                  height: 12,
                ),


                // ==========================================
                // STOP AUDIO
                // ==========================================

                SizedBox(

                  width:
                      double.infinity,

                  child:
                      OutlinedButton.icon(

                    onPressed:
                        _isPlaying
                            ? _stopAudio
                            : null,

                    icon:
                        const Icon(
                      Icons.stop,
                    ),

                    label:
                        const Text(
                      'STOP AUDIO',
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}