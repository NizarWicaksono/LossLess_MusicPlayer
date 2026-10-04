import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() {
  runApp(const HiResPlayerApp());
}

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

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {

  static const MethodChannel _channel = MethodChannel('native_audio');

  String _status = 'Audio Engine belum dites';

  Future<void> _testNativeAudio() async {
    try {
      final result = await _channel.invokeMethod<String>(
        'getEngineStatus',
      );

      setState(() {
        _status = result ?? 'Tidak ada response';
      });

    } on PlatformException catch (e) {
      setState(() {
        _status = 'Error: ${e.message}';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Hi-Res Player'),
      ),

      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [

              const Icon(
                Icons.audiotrack,
                size: 80,
              ),

              const SizedBox(height: 24),

              const Text(
                'Native Audio Engine',
                style: TextStyle(
                  fontSize: 24,
                  fontWeight: FontWeight.bold,
                ),
              ),

              const SizedBox(height: 16),

              Text(
                _status,
                textAlign: TextAlign.center,
                style: const TextStyle(
                  fontSize: 18,
                ),
              ),

              const SizedBox(height: 32),

              ElevatedButton(
                onPressed: _testNativeAudio,
                child: const Text(
                  'TEST AUDIO ENGINE',
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}