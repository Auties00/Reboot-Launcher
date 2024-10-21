import 'dart:collection';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:file_picker/file_picker.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/scheduler.dart';
import 'package:win32/win32.dart';

final RegExp _winBuildRegex = RegExp(r'(?<=\(Build )(.*)(?=\))');

int? get windowsBuild {
  final result = _winBuildRegex.firstMatch(Platform.operatingSystemVersion)?.group(1);
  if (result == null) {
    return null;
  }

  return int.tryParse(result);
}

bool get isWin11 {
  final intBuild = windowsBuild;
  return intBuild != null && intBuild > 22000;
}

Future<String?> openFolderPicker(String title) async {
  FilePicker.platform = FilePickerWindows();
  return await FilePicker.platform.getDirectoryPath(dialogTitle: title);
}

Future<String?> openFilePicker(String extension) async {
  FilePicker.platform = FilePickerWindows();
  var result = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowMultiple: false,
      allowedExtensions: [extension]
  );
  if(result == null || result.files.isEmpty){
    return null;
  }

  return result.files.first.path;
}

bool get isDarkMode =>
    SchedulerBinding.instance.platformDispatcher.platformBrightness.isDark;

class _ServiceProvider10 extends IUnknown {
  static const String _CLSID = "{C2F03A33-21F5-47FA-B4BB-156362A2F239}";
  static const String _IID = "{6D5140C1-7436-11CE-8034-00AA006009FA}";

  _ServiceProvider10._internal(Pointer<COMObject> ptr) : super(ptr);

  factory _ServiceProvider10.createInstance() =>
      _ServiceProvider10._internal(COMObject.createFromID(_CLSID, _IID));

  Pointer<COMObject> queryService(String classId, String instanceId) {
    final result = calloc<COMObject>();
    final code = (ptr.ref.vtable + 3)
        .cast<
        Pointer<
            NativeFunction<
                HRESULT Function(Pointer, Pointer<GUID>, Pointer<GUID>,
                    Pointer<COMObject>)>>>()
        .value
        .asFunction<
        int Function(Pointer, Pointer<GUID>, Pointer<GUID>,
            Pointer<COMObject>)>()(ptr.ref.lpVtbl,
        GUIDFromString(classId), GUIDFromString(instanceId), result);
    if (code != 0) {
      free(result);
      throw WindowsException(code);
    }

    return result;
  }
}

class IVirtualDesktop extends IUnknown {
  static const String _CLSID = "{3F07F4BE-B107-441A-AF0F-39D82529072C}";

  IVirtualDesktop._internal(super.ptr);

  String getName() {
    final result = calloc<HSTRING>();
    final code = (ptr.ref.vtable + 5)
        .cast<
        Pointer<
            NativeFunction<HRESULT Function(Pointer, Pointer<HSTRING>)>>>()
        .value
        .asFunction<
        int Function(Pointer, Pointer<HSTRING>)>()(ptr.ref.lpVtbl, result);
    if (code != 0) {
      free(result);
      throw WindowsException(code);
    }

    return _convertFromHString(result.value);
  }
}

class IApplicationView extends IUnknown {
  // static const String _CLSID = "{372E1D3B-38D3-42E4-A15B-8AB2B178F513}";

  IApplicationView._internal(super.ptr);
}

class _IObjectArray extends IUnknown {
  _IObjectArray(super.ptr);

  int getCount() {
    final result = calloc<Int32>();
    final code = (ptr.ref.vtable + 3)
        .cast<
        Pointer<
            NativeFunction<HRESULT Function(Pointer, Pointer<Int32>)>>>()
        .value
        .asFunction<
        int Function(Pointer, Pointer<Int32>)>()(ptr.ref.lpVtbl, result);
    if (code != 0) {
      free(result);
      throw WindowsException(code);
    }

    return result.value;
  }

  Pointer<COMObject> getAt(int index, String guid) {
    final result = calloc<COMObject>();
    final code = (ptr.ref.vtable + 4)
        .cast<
        Pointer<
            NativeFunction<
                HRESULT Function(Pointer, Int32 index, Pointer<GUID>,
                    Pointer<COMObject>)>>>()
        .value
        .asFunction<
        int Function(
            Pointer, int index, Pointer<GUID>, Pointer<COMObject>)>()(
        ptr.ref.lpVtbl, index, GUIDFromString(guid), result);
    if (code != 0) {
      free(result);
      throw WindowsException(code);
    }

    return result;
  }
}

typedef _IObjectMapper<T> = T Function(Pointer<COMObject>);

class _IObjectArrayList<T> extends ListBase<T> {
  final _IObjectArray _array;
  final String _guid;
  final _IObjectMapper<T> _mapper;

  _IObjectArrayList(
      {required _IObjectArray array,
        required String guid,
        required _IObjectMapper<T> mapper})
      : _array = array,
        _guid = guid,
        _mapper = mapper;

  @override
  int get length => _array.getCount();

  @override
  set length(int newLength) {
    throw UnsupportedError("Immutable list");
  }

  @override
  T operator [](int index) => _mapper(_array.getAt(index, _guid));

  @override
  void operator []=(int index, T value) {
    throw UnsupportedError("Immutable list");
  }
}

class _IVirtualDesktopManagerInternal extends IUnknown {
  static const String _CLSID = "{C5E0CDCA-7B6E-41B2-9FC4-D93975CC467B}";
  static const String _IID_WIN10 = "{F31574D6-B682-4CDC-BD56-1827860ABEC6}";
  static const String _IID_WIN_21H2 = "{B2F925B9-5A0F-4D2E-9F4D-2B1507593C10}";
  static const String _IID_WIN_23H2 = "{A3175F2D-239C-4BD2-8AA0-EEBA8B0B138E}";
  static const String _IID_WIN_23H2_3085 = "{53F5CA0B-158F-4124-900C-057158060B27}";

  _IVirtualDesktopManagerInternal._internal(super.ptr);

  int getDesktopsCount() {
    final result = calloc<Int32>();
    final code = (ptr.ref.vtable + 3)
        .cast<
        Pointer<
            NativeFunction<HRESULT Function(Pointer, Pointer<Int32>)>>>()
        .value
        .asFunction<
        int Function(Pointer, Pointer<Int32>)>()(ptr.ref.lpVtbl, result);
    if (code != 0) {
      free(result);
      throw WindowsException(code);
    }

    return result.value;
  }

  List<IVirtualDesktop> getDesktops() {
    final result = calloc<COMObject>();
    final code = (ptr.ref.vtable + 7)
        .cast<
        Pointer<
            NativeFunction<
                HRESULT Function(Pointer, Pointer<COMObject>)>>>()
        .value
        .asFunction<int Function(Pointer, Pointer<COMObject>)>()(
        ptr.ref.lpVtbl, result);
    if (code != 0) {
      free(result);
      throw WindowsException(code);
    }

    final array = _IObjectArray(result);
    return _IObjectArrayList(
        array: array,
        guid: IVirtualDesktop._CLSID,
        mapper: (comObject) => IVirtualDesktop._internal(comObject));
  }

  void moveWindowToDesktop(IApplicationView view, IVirtualDesktop desktop) {
    final code = (ptr.ref.vtable + 4)
        .cast<
        Pointer<
            NativeFunction<
                Int32 Function(Pointer, COMObject, COMObject)>>>()
        .value
        .asFunction<int Function(Pointer, COMObject, COMObject)>()(
        ptr.ref.lpVtbl, view.ptr.ref, desktop.ptr.ref);
    if (code != 0) {
      throw WindowsException(code, message: "Cannot move window");
    }
  }

  IVirtualDesktop createDesktop() {
    final result = calloc<COMObject>();
    final code = (ptr.ref.vtable + 10)
        .cast<
        Pointer<
            NativeFunction<
                HRESULT Function(Pointer, Pointer<COMObject>)>>>()
        .value
        .asFunction<int Function(Pointer, Pointer<COMObject>)>()(
        ptr.ref.lpVtbl, result);
    if (code != 0) {
      free(result);
      throw WindowsException(code);
    }

    return IVirtualDesktop._internal(result);
  }

  void removeDesktop(IVirtualDesktop desktop, IVirtualDesktop fallback) {
    final code = (ptr.ref.vtable + 12)
        .cast<
        Pointer<
            NativeFunction<
                HRESULT Function(Pointer, COMObject, COMObject)>>>()
        .value
        .asFunction<int Function(Pointer, COMObject, COMObject)>()(
        ptr.ref.lpVtbl, desktop.ptr.ref, fallback.ptr.ref);
    if (code != 0) {
      throw WindowsException(code);
    }
  }

  void setDesktopName(IVirtualDesktop desktop, String newName) {
    final code =
    (ptr.ref.vtable + 15)
        .cast<
        Pointer<
            NativeFunction<
                HRESULT Function(Pointer, COMObject, Int8)>>>()
        .value
        .asFunction<int Function(Pointer, COMObject, int)>()(
        ptr.ref.lpVtbl, desktop.ptr.ref, _convertToHString(newName));
    if (code != 0) {
      throw WindowsException(code);
    }
  }
}

class _IApplicationViewCollection extends IUnknown {
  static const String _CLSID = "{1841C6D7-4F9D-42C0-AF41-8747538F10E5}";
  static const String _IID = "{1841C6D7-4F9D-42C0-AF41-8747538F10E5}";

  _IApplicationViewCollection._internal(super.ptr);

  IApplicationView? getViewForHWnd(int HWnd) {
    final result = calloc<COMObject>();
    final code =
    (ptr.ref.vtable + 6)
        .cast<
        Pointer<
            NativeFunction<
                HRESULT Function(
                    Pointer, IntPtr, Pointer<COMObject>)>>>()
        .value
        .asFunction<int Function(Pointer, int, Pointer<COMObject>)>()(
        ptr.ref.lpVtbl, HWnd, result);
    if (code != 0) {
      free(result);
      return null;
    }

    return IApplicationView._internal(result);
  }
}

final class Win32Process extends Struct {
  @Uint32()
  external int pid;

  @Uint32()
  external int HWndLength;

  external Pointer<Uint32> HWnd;

  external Pointer<Utf16> excluded;
}

int _filter(int HWnd, int lParam) {
  final structure = Pointer.fromAddress(lParam).cast<Win32Process>();
  if(structure.ref.excluded != nullptr) {
    final excludedWindowName = structure.ref.excluded.toDartString();
    final windowNameLength = GetWindowTextLength(HWnd);
    if(windowNameLength > 0) {
      final windowNamePointer = calloc<Uint16>(windowNameLength + 1).cast<Utf16>();
      GetWindowText(HWnd, windowNamePointer, windowNameLength);
      final windowName = windowNamePointer.toDartString(length: windowNameLength);
      if(windowName.toLowerCase().contains(excludedWindowName.toLowerCase())) {
        return TRUE;
      }
    }
  }

  final pidPointer = calloc<Uint32>();
  GetWindowThreadProcessId(HWnd, pidPointer);
  final pid = pidPointer.value;
  if (pid == structure.ref.pid) {
    final length = structure.ref.HWndLength;
    final newLength = length + 1;
    final ptr = malloc.allocate<Uint32>(sizeOf<Uint32>() * newLength);
    final list = structure.ref.HWnd.asTypedList(length);
    for (var i = 0; i < list.length; i++) {
      (ptr + i).value = list[i];
    }
    ptr[list.length] = HWnd;
    structure.ref.HWndLength = newLength;
    free(structure.ref.HWnd);
    structure.ref.HWnd = ptr;
  }

  free(pidPointer);
  return TRUE;
}

List<int> _getHWnds(int pid, String? excludedWindowName) {
  final result = calloc<Win32Process>();
  result.ref.pid = pid;
  if(excludedWindowName != null) {
    result.ref.excluded = excludedWindowName.toNativeUtf16();
  }

  EnumWindows(Pointer.fromFunction<WNDENUMPROC>(_filter, TRUE), result.address);
  final length = result.ref.HWndLength;
  final HWndsPointer = result.ref.HWnd;
  if(HWndsPointer == nullptr) {
    calloc.free(result);
    return [];
  }

  final HWnds = HWndsPointer.asTypedList(length)
      .toList(growable: false);
  calloc.free(result);
  return HWnds;
}

class VirtualDesktopManager {
  static VirtualDesktopManager? _instance;

  final _IVirtualDesktopManagerInternal windowManager;
  final _IApplicationViewCollection applicationViewCollection;

  VirtualDesktopManager._internal(this.windowManager, this.applicationViewCollection);

  factory VirtualDesktopManager.getInstance() {
    if (_instance != null) {
      return _instance!;
    }

    final hr = CoInitializeEx(
        nullptr, COINIT.COINIT_APARTMENTTHREADED | COINIT.COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
      throw WindowsException(hr);
    }

    final shell = _ServiceProvider10.createInstance();
    final windowManager = _createWindowManager(shell);
    final applicationViewCollection = _IApplicationViewCollection._internal(
        shell.queryService(_IApplicationViewCollection._CLSID,
            _IApplicationViewCollection._IID));
    return _instance =
        VirtualDesktopManager._internal(windowManager, applicationViewCollection);
  }

  static _IVirtualDesktopManagerInternal _createWindowManager(_ServiceProvider10 shell) {
    final build = windowsBuild;
    if(build == null || build < 19044) {
      return _IVirtualDesktopManagerInternal._internal(
          shell.queryService(_IVirtualDesktopManagerInternal._CLSID,
              _IVirtualDesktopManagerInternal._IID_WIN10));
    }else if(build >= 19044 && build < 22631) {
      return _IVirtualDesktopManagerInternal._internal(
          shell.queryService(_IVirtualDesktopManagerInternal._CLSID,
              _IVirtualDesktopManagerInternal._IID_WIN_21H2));
    }else if(build >= 22631 && build < 22631) {
      return _IVirtualDesktopManagerInternal._internal(
          shell.queryService(_IVirtualDesktopManagerInternal._CLSID,
              _IVirtualDesktopManagerInternal._IID_WIN_23H2));
    }else {
      return _IVirtualDesktopManagerInternal._internal(
          shell.queryService(_IVirtualDesktopManagerInternal._CLSID,
              _IVirtualDesktopManagerInternal._IID_WIN_23H2_3085));
    }
  }

  int getDesktopsCount() => windowManager.getDesktopsCount();

  List<IVirtualDesktop> getDesktops() => windowManager.getDesktops();

  Future<bool> moveWindowToDesktop(int pid, IVirtualDesktop desktop, {Duration pollTime = const Duration(seconds: 1), int remainingPolls = 10, String? excludedWindowName}) async {
    for(final hWND in _getHWnds(pid, excludedWindowName)) {
      final window = applicationViewCollection.getViewForHWnd(hWND);
      if(window != null) {
        windowManager.moveWindowToDesktop(window, desktop);
        return true;
      }
    }

    if(remainingPolls <= 0) {
      return false;
    }

    await Future.delayed(pollTime);
    return await moveWindowToDesktop(
        pid,
        desktop,
        pollTime: pollTime,
        remainingPolls: remainingPolls - 1
    );
  }

  IVirtualDesktop createDesktop() => windowManager.createDesktop();

  void removeDesktop(IVirtualDesktop desktop, [IVirtualDesktop? fallback]) {
    fallback ??= getDesktops().first;
    return windowManager.removeDesktop(desktop, fallback);
  }

  void setDesktopName(IVirtualDesktop desktop, String newName) =>
      windowManager.setDesktopName(desktop, newName);
}

String _convertFromHString(int hstring) =>
    WindowsGetStringRawBuffer(hstring, nullptr).toDartString();

int _convertToHString(String string) {
  final hString = calloc<HSTRING>();
  final stringPtr = string.toNativeUtf16();
  try {
    final hr = WindowsCreateString(stringPtr, string.length, hString);
    if (FAILED(hr)) throw WindowsException(hr);
    return hString.value;
  } finally {
    free(stringPtr);
    free(hString);
  }
}