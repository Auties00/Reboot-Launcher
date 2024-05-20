import 'dart:convert';
import 'dart:math';
import 'dart:typed_data';

import 'package:bcrypt/bcrypt.dart';
import 'package:pointycastle/export.dart';

const int _ivLength = 16;
const int _keyLength = 32;

String hashPassword(String plaintext) => BCrypt.hashpw(plaintext, BCrypt.gensalt());

bool checkPassword(String password, String hashedText) {
  try {
    return BCrypt.checkpw(password, hashedText);
  }catch(error) {
    return false;
  }
}

String aes256Encrypt(String plainText, String password) {
  final random = Random.secure();
  final iv = Uint8List.fromList(List.generate(_ivLength, (index) => random.nextInt(256)));
  final keyDerivationData = Uint8List.fromList(utf8.encode(password));
  final derive = PBKDF2KeyDerivator(HMac(SHA256Digest(), _ivLength * 8));
  var params = Pbkdf2Parameters(iv, _ivLength * 8, _keyLength);
  derive.init(params);
  final key = derive.process(keyDerivationData);
  final cipherParams = PaddedBlockCipherParameters(
    KeyParameter(key),
    null,
  );
  final aes = AESEngine();
  final paddingCipher = PaddedBlockCipherImpl(PKCS7Padding(), aes);
  paddingCipher.init(true, cipherParams);
  final plainBytes = Uint8List.fromList(utf8.encode(plainText));
  final encryptedBytes = paddingCipher.process(plainBytes);
  return base64.encode([...iv, ...encryptedBytes]);
}

String aes256Decrypt(String encryptedText, String password) {
  final encryptedBytes = base64.decode(encryptedText);
  final salt = encryptedBytes.sublist(0, _ivLength);
  final payload = encryptedBytes.sublist(_ivLength);
  final keyDerivationData = Uint8List.fromList(utf8.encode(password));
  final derive = PBKDF2KeyDerivator(HMac(SHA256Digest(), _ivLength * 8));
  var params = Pbkdf2Parameters(salt, _ivLength * 8, _keyLength);
  derive.init(params);
  final key = derive.process(keyDerivationData);
  final cipherParams = PaddedBlockCipherParameters(
    KeyParameter(key),
    null,
  );
  final aes = AESEngine();
  final paddingCipher = PaddedBlockCipherImpl(PKCS7Padding(), aes);
  paddingCipher.init(false, cipherParams);
  final decryptedBytes = paddingCipher.process(payload);
  return utf8.decode(decryptedBytes);
}
