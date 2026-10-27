// Minimal radio_link v3 client in Dart, for validating that the chosen IPC
// (localhost TCP) is usable from the Dart application layer.
//
// Usage: dart run tools/radio_link_client.dart [seconds] [channel]
import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

const host = '127.0.0.1';
const port = 49876;

Future<Socket> connectWithRetry() async {
  final deadline = DateTime.now().add(const Duration(seconds: 90));
  Object? last;
  while (DateTime.now().isBefore(deadline)) {
    try {
      return await Socket.connect(host, port,
          timeout: const Duration(seconds: 2));
    } catch (e) {
      last = e;
      await Future<void>.delayed(const Duration(milliseconds: 500));
    }
  }
  throw StateError('cannot connect to $host:$port: $last');
}

void sendMessage(Socket sock, int type, List<int> payload) {
  final header = BytesBuilder();
  header.add(Uint8List(2)..buffer.asByteData().setUint16(0, type, Endian.little));
  header.add(Uint8List(2)); // reserved
  final length = Uint8List(4)..buffer.asByteData().setUint32(0, payload.length, Endian.little);
  header.add(length);
  header.add(payload);
  sock.add(header.toBytes());
}

Future<void> main(List<String> args) async {
  final seconds = args.isNotEmpty ? int.parse(args[0]) : 5;
  final channel = args.length > 1 ? int.parse(args[1]) : 0;
  final sock = await connectWithRetry();
  print('connected to $host:$port');

  final buffer = BytesBuilder();
  var hello = false;
  var rx = 0;
  var bytes = 0;

  sock.listen((data) {
    buffer.add(data);
    while (true) {
      final all = buffer.toBytes();
      if (all.length < 8) return;
      final dataView = ByteData.sublistView(all);
      final type = dataView.getUint16(0, Endian.little);
      final length = dataView.getUint32(4, Endian.little);
      if (all.length < 8 + length) return;
      final payload = all.sublist(8, 8 + length);
      buffer
        ..clear()
        ..add(all.sublist(8 + length));
      if (!hello) {
        if (type != 1 || payload.length != 8) {
          print('unexpected first message type=$type len=${payload.length}');
          exit(1);
        }
        final view = ByteData.sublistView(Uint8List.fromList(payload));
        final version = view.getUint32(0, Endian.little);
        final ch = payload[4];
        final width = payload[5];
        print('HELLO version=$version channel=$ch width=${width}MHz');
        hello = true;
      } else if (type == 2) {
        rx++;
        bytes += payload.length;
      }
    }
  });

  // Give the radio a moment to deliver HELLO, then optionally retune.
  await Future<void>.delayed(const Duration(milliseconds: 300));
  if (channel > 0) {
    sendMessage(sock, 4, [channel]);
    print('SET_CHANNEL $channel');
  }

  await Future<void>.delayed(Duration(seconds: seconds));
  print(jsonEncode({'dartRxFrames': rx, 'dartRxBytes': bytes}));
  sock.destroy();
  exit(0);
}
