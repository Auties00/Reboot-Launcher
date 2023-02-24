import 'dart:async';
import 'dart:io';

import 'package:jaguar/jaguar.dart';

class EmbeddedErrorWriter extends ErrorWriter {
  static const String _errorName404 = "errors.com.lawinserver.common.not_found";
  static const String _errorName500 = "errors.com.lawinserver.common.error";
  static const String _errorCode = "1004";

  @override
  FutureOr<Response> make404(Context ctx) {
    stdout.writeln("Unknown path: ${ctx.uri} with method ${ctx.method}");
    ctx.response.headers.set('X-Epic-Error-Name', _errorName404);
    ctx.response.headers.set('X-Epic-Error-Code', _errorCode);
    return Response.json(
        statusCode: 204,
        {
          "errorCode": _errorName404,
          "errorMessage": "Sorry the resource you were trying to find could not be found",
          "numericErrorCode": _errorCode,
          "originatingService": "any",
          "intent": "prod"
        }
    );
  }

  @override
  FutureOr<Response> make500(Context ctx, Object error, [StackTrace? stack]) {
    ctx.response.headers.set('X-Epic-Error-Name', _errorName500);
    ctx.response.headers.set('X-Epic-Error-Code', _errorCode);
    return Response.json(
        statusCode: 500,
        {
          "errorCode": _errorName500,
          "errorMessage": "Sorry the resource you were trying to find threw an error",
          "numericErrorCode": _errorCode,
          "originatingService": "any",
          "intent": "prod"
        }
    );
  }
}