// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';
import 'package:ui/ui.dart' as ui;

import '../engine.dart'  show registerHotRestartListener;
import 'browser_detection.dart';
import 'dom.dart';
import 'key_map.g.dart';
import 'platform_dispatcher.dart';
import 'safe_browser_api.dart';
import 'semantics.dart';

typedef _VoidCallback = void Function();
typedef ValueGetter<T> = T Function();
typedef _ModifierGetter = bool Function(FlutterHtmlKeyboardEvent event);

// Set this flag to true to see all the fired events in the console.
const bool _debugLogKeyEvents = false;

const int _kLocationLeft = 1;
const int _kLocationRight = 2;

final int _kLogicalAltLeft = kWebLogicalLocationMap['Alt']![_kLocationLeft]!;
final int _kLogicalAltRight = kWebLogicalLocationMap['Alt']![_kLocationRight]!;
final int _kLogicalControlLeft = kWebLogicalLocationMap['Control']![_kLocationLeft]!;
final int _kLogicalControlRight = kWebLogicalLocationMap['Control']![_kLocationRight]!;
final int _kLogicalShiftLeft = kWebLogicalLocationMap['Shift']![_kLocationLeft]!;
final int _kLogicalShiftRight = kWebLogicalLocationMap['Shift']![_kLocationRight]!;
final int _kLogicalMetaLeft = kWebLogicalLocationMap['Meta']![_kLocationLeft]!;
final int _kLogicalMetaRight = kWebLogicalLocationMap['Meta']![_kLocationRight]!;

final int _kPhysicalAltLeft = kWebToPhysicalKey['AltLeft']!;
final int _kPhysicalAltRight = kWebToPhysicalKey['AltRight']!;
final int _kPhysicalControlLeft = kWebToPhysicalKey['ControlLeft']!;
final int _kPhysicalControlRight = kWebToPhysicalKey['ControlRight']!;
final int _kPhysicalShiftLeft = kWebToPhysicalKey['ShiftLeft']!;
final int _kPhysicalShiftRight = kWebToPhysicalKey['ShiftRight']!;
final int _kPhysicalMetaLeft = kWebToPhysicalKey['MetaLeft']!;
final int _kPhysicalMetaRight = kWebToPhysicalKey['MetaRight']!;

// Map logical keys for modifier keys to the functions that can get their
// modifier flag out of an event.
final Map<int, _ModifierGetter> _kLogicalKeyToModifierGetter = <int, _ModifierGetter>{
  _kLogicalAltLeft: (FlutterHtmlKeyboardEvent event) => event.altKey,
  _kLogicalAltRight: (FlutterHtmlKeyboardEvent event) => event.altKey,
  _kLogicalControlLeft: (FlutterHtmlKeyboardEvent event) => event.ctrlKey,
  _kLogicalControlRight: (FlutterHtmlKeyboardEvent event) => event.ctrlKey,
  _kLogicalShiftLeft: (FlutterHtmlKeyboardEvent event) => event.shiftKey,
  _kLogicalShiftRight: (FlutterHtmlKeyboardEvent event) => event.shiftKey,
  _kLogicalMetaLeft: (FlutterHtmlKeyboardEvent event) => event.metaKey,
  _kLogicalMetaRight: (FlutterHtmlKeyboardEvent event) => event.metaKey,
};

// ASCII for a, z, A, and Z
const int _kCharLowerA = 0x61;
const int _kCharLowerZ = 0x7a;
const int _kCharUpperA = 0x41;
const int _kCharUpperZ = 0x5a;
bool isAlphabet(int charCode) {
  return (charCode >= _kCharLowerA && charCode <= _kCharLowerZ)
      || (charCode >= _kCharUpperA && charCode <= _kCharUpperZ);
}

const String _kPhysicalCapsLock = 'CapsLock';

const String _kLogicalDead = 'Dead';

const int _kWebKeyIdPlane = 0x1700000000;

// Bits in a Flutter logical event to generate the logical key for dead keys.
//
// Logical keys for dead keys are generated by annotating physical keys with
// modifiers (see `_getLogicalCode`).
const int _kDeadKeyCtrl = 0x10000000;
const int _kDeadKeyShift = 0x20000000;
const int _kDeadKeyAlt = 0x40000000;
const int _kDeadKeyMeta = 0x80000000;

const ui.KeyData _emptyKeyData = ui.KeyData(
  type: ui.KeyEventType.down,
  timeStamp: Duration.zero,
  logical: 0,
  physical: 0,
  character: null,
  synthesized: false,
);

typedef DispatchKeyData = bool Function(ui.KeyData data);

/// Converts a floating number timestamp (in milliseconds) to a [Duration] by
/// splitting it into two integer components: milliseconds + microseconds.
Duration _eventTimeStampToDuration(num milliseconds) {
  final int ms = milliseconds.toInt();
  final int micro = ((milliseconds - ms) * Duration.microsecondsPerMillisecond).toInt();
  return Duration(milliseconds: ms, microseconds: micro);
}

class KeyboardBinding {
  KeyboardBinding._() {
    _setup();
  }

  /// The singleton instance of this object.
  static KeyboardBinding? get instance => _instance;
  static KeyboardBinding? _instance;

  static void initInstance() {
    if (_instance == null) {
      _instance = KeyboardBinding._();
      assert(() {
        registerHotRestartListener(_instance!._reset);
        return true;
      }());
    }
  }

  KeyboardConverter get converter => _converter;
  late final KeyboardConverter _converter;
  final Map<String, DomEventListener> _listeners = <String, DomEventListener>{};

  void _addEventListener(String eventName, DomEventListener handler) {
    dynamic loggedHandler(DomEvent event) {
      if (_debugLogKeyEvents) {
        print(event.type);
      }
      if (EngineSemanticsOwner.instance.receiveGlobalEvent(event)) {
        return handler(event);
      }
      return null;
    }

    final DomEventListener wrappedHandler = allowInterop(loggedHandler);
    assert(!_listeners.containsKey(eventName));
    _listeners[eventName] = wrappedHandler;
    domWindow.addEventListener(eventName, wrappedHandler, true);
  }

  /// Remove all active event listeners.
  void _clearListeners() {
    _listeners.forEach((String eventName, DomEventListener listener) {
      domWindow.removeEventListener(eventName, listener, true);
    });
    _listeners.clear();
  }
  bool _onKeyData(ui.KeyData data) {
    bool? result;
    // This callback is designed to be invoked synchronously. This is enforced
    // by `result`, which starts null and is asserted non-null when returned.
    EnginePlatformDispatcher.instance.invokeOnKeyData(data,
      (bool handled) { result = handled; });
    return result!;
  }

  void _setup() {
    _addEventListener('keydown', allowInterop((DomEvent event) {
      return _converter.handleEvent(FlutterHtmlKeyboardEvent(event as DomKeyboardEvent));
    }));
    _addEventListener('keyup', allowInterop((DomEvent event) {
      return _converter.handleEvent(FlutterHtmlKeyboardEvent(event as DomKeyboardEvent));
    }));
    _converter = KeyboardConverter(_onKeyData, onMacOs: operatingSystem == OperatingSystem.macOs);
  }

  void _reset() {
    _clearListeners();
    _converter.dispose();
  }
}

class AsyncKeyboardDispatching {
  AsyncKeyboardDispatching({
    required this.keyData,
    this.callback,
  });

  final ui.KeyData keyData;
  final _VoidCallback? callback;
}

// A wrapper of [DomKeyboardEvent] with reduced methods delegated to the event
// for the convenience of testing.
class FlutterHtmlKeyboardEvent {
  FlutterHtmlKeyboardEvent(this._event);

  final DomKeyboardEvent _event;

  String get type => _event.type;
  String? get code => _event.code;
  String? get key => _event.key;
  int get keyCode => _event.keyCode;
  bool? get repeat => _event.repeat;
  int? get location => _event.location;
  num? get timeStamp => _event.timeStamp;
  bool get altKey => _event.altKey;
  bool get ctrlKey => _event.ctrlKey;
  bool get shiftKey => _event.shiftKey;
  bool get metaKey => _event.metaKey;

  bool getModifierState(String key) => _event.getModifierState(key);
  void preventDefault() => _event.preventDefault();
  bool get defaultPrevented => _event.defaultPrevented;
}

// Reads [DomKeyboardEvent], then [dispatches ui.KeyData] accordingly.
//
// The events are read through [handleEvent], and dispatched through the
// [dispatchKeyData] as given in the constructor. Some key data might be
// dispatched asynchronously.
class KeyboardConverter {
  KeyboardConverter(this.performDispatchKeyData, {this.onMacOs = false});

  final DispatchKeyData performDispatchKeyData;
  final bool onMacOs;

  // The `performDispatchKeyData` wrapped with tracking logic.
  //
  // It is non-null only during `handleEvent`. All events during `handleEvent`
  // should be dispatched with `_dispatchKeyData`, others with
  // `performDispatchKeyData`.
  DispatchKeyData? _dispatchKeyData;

  bool _disposed = false;
  void dispose() {
    _disposed = true;
  }

  // On macOS, CapsLock behaves differently in that, a keydown event occurs when
  // the key is pressed and the light turns on, while a keyup event occurs when the
  // key is pressed and the light turns off. Flutter considers both events as
  // key down, and synthesizes immediate cancel events following them. The state
  // of "whether CapsLock is on" should be accessed by "activeLocks".
  bool _shouldSynthesizeCapsLockUp() {
    return onMacOs;
  }

  // ## About Key guards
  //
  // When the user enters a browser/system shortcut (e.g. `Cmd+Alt+i`) the
  // browser doesn't send a keyup for it. This puts the framework in a corrupt
  // state because it thinks the key was never released.
  //
  // To avoid this, we rely on the fact that browsers send repeat events
  // while the key is held down by the user. If we don't receive a repeat
  // event within a specific duration ([_keydownCancelDurationMac]) we assume
  // the user has released the key and we synthesize a keyup event.
  bool _shouldDoKeyGuard() {
    return onMacOs;
  }

  /// After a keydown is received, this is the duration we wait for a repeat event
  /// before we decide to synthesize a keyup event.
  ///
  /// This value is only for macOS, where the keyboard repeat delay goes up to
  /// 2000ms.
  static const Duration _kKeydownCancelDurationMac = Duration(milliseconds: 2000);

  static int _getPhysicalCode(String code) {
    return kWebToPhysicalKey[code] ?? (code.hashCode + _kWebKeyIdPlane);
  }

  static int _getModifierMask(FlutterHtmlKeyboardEvent event) {
    final bool altDown = event.altKey;
    final bool ctrlDown = event.ctrlKey;
    final bool shiftDown = event.shiftKey;
    final bool metaDown = event.metaKey;
    return (altDown ? _kDeadKeyAlt : 0) +
           (ctrlDown ? _kDeadKeyCtrl : 0) +
           (shiftDown ? _kDeadKeyShift : 0) +
           (metaDown ? _kDeadKeyMeta : 0);
  }

  // Whether `event.key` should be considered a key name.
  //
  // The `event.key` can either be a key name or the printable character. If the
  // first character is an alphabet, it must be either 'A' to 'Z' ( and return
  // true), or be a key name (and return false). Otherwise, return true.
  static bool _eventKeyIsKeyname(String key) {
    assert(key.isNotEmpty);
    return isAlphabet(key.codeUnitAt(0)) && key.length > 1;
  }

  static int _characterToLogicalKey(String key) {
    // Assume the length being <= 2 to be sufficient in all cases. If not,
    // extend the algorithm.
    assert(key.length <= 2);
    int result = key.codeUnitAt(0) & 0xffff;
    if (key.length == 2) {
      result += key.codeUnitAt(1) << 16;
    }
    // Convert upper letters to lower letters
    if (result >= _kCharUpperA && result <= _kCharUpperZ) {
      result = result + _kCharLowerA - _kCharUpperA;
    }
    return result;
  }

  static int _deadKeyToLogicalKey(int physicalKey, FlutterHtmlKeyboardEvent event) {
    // 'Dead' is used to represent dead keys, such as a diacritic to the
    // following base letter (such as Option-e results in ´).
    //
    // Assume they can be told apart with the physical key and the modifiers
    // pressed.
    return physicalKey + _getModifierMask(event) + _kWebKeyIdPlane;
  }

  static int _otherLogicalKey(String key) {
    return kWebToLogicalKey[key] ?? (key.hashCode + _kWebKeyIdPlane);
  }

  // Map from pressed physical key to corresponding pressed logical key.
  //
  // Multiple physical keys can be mapped to the same logical key, usually due
  // to positioned keys (left/right/numpad) or multiple keyboards.
  final Map<int, int> _pressingRecords = <int, int>{};

  // Schedule the dispatching of an event in the future. The `callback` will
  // invoked before that.
  //
  // Returns a callback that cancels the schedule. Disposal of
  // `KeyBoardConverter` also cancels the shedule automatically.
  _VoidCallback _scheduleAsyncEvent(Duration duration, ValueGetter<ui.KeyData> getData, _VoidCallback callback) {
    bool canceled = false;
    Future<void>.delayed(duration).then<void>((_) {
      if (!canceled && !_disposed) {
        callback();
        // This dispatch is performed asynchronously, therefore should not use
        // `_dispatchKeyData`.
        performDispatchKeyData(getData());
      }
    });
    return () { canceled = true; };
  }

  final Map<int, _VoidCallback> _keyGuards = <int, _VoidCallback>{};
  // Call this method on the down or repeated event of a non-modifier key.
  void _startGuardingKey(int physicalKey, int logicalKey, Duration currentTimeStamp) {
    if (!_shouldDoKeyGuard()) {
      return;
    }
    final _VoidCallback cancelingCallback = _scheduleAsyncEvent(
      _kKeydownCancelDurationMac,
      () => ui.KeyData(
        timeStamp: currentTimeStamp + _kKeydownCancelDurationMac,
        type: ui.KeyEventType.up,
        physical: physicalKey,
        logical: logicalKey,
        character: null,
        synthesized: true,
      ),
      () {
        _pressingRecords.remove(physicalKey);
      }
    );
    _keyGuards.remove(physicalKey)?.call();
    _keyGuards[physicalKey] = cancelingCallback;
  }
  // Call this method on an up event event of a non-modifier key.
  void _stopGuardingKey(int physicalKey) {
    _keyGuards.remove(physicalKey)?.call();
  }

  void _handleEvent(FlutterHtmlKeyboardEvent event) {
    final Duration timeStamp = _eventTimeStampToDuration(event.timeStamp!);

    final String eventKey = event.key!;

    final int physicalKey = _getPhysicalCode(event.code!);
    final bool logicalKeyIsCharacter = !_eventKeyIsKeyname(eventKey);
    final String? character = logicalKeyIsCharacter ? eventKey : null;
    final int logicalKey = () {
      if (kWebLogicalLocationMap.containsKey(event.key)) {
        final int? result = kWebLogicalLocationMap[event.key!]?[event.location!];
        assert(result != null, 'Invalid modifier location: ${event.key}, ${event.location}');
        return result!;
      }
      if (character != null) {
        return _characterToLogicalKey(character);
      }
      if (eventKey == _kLogicalDead) {
        return _deadKeyToLogicalKey(physicalKey, event);
      }
      return _otherLogicalKey(eventKey);
    }();

    assert(event.type == 'keydown' || event.type == 'keyup');
    final bool isPhysicalDown = event.type == 'keydown' ||
      // On macOS, both keydown and keyup events of CapsLock should be considered keydown,
      // followed by an immediate cancel event.
      (_shouldSynthesizeCapsLockUp() && event.code! == _kPhysicalCapsLock);

    final ui.KeyEventType type;

    if (_shouldSynthesizeCapsLockUp() && event.code! == _kPhysicalCapsLock) {
      // Case 1: Handle CapsLock on macOS
      //
      // On macOS, both keydown and keyup events of CapsLock are considered
      // keydown, followed by an immediate synchronized up event.

      _scheduleAsyncEvent(
        Duration.zero,
        () => ui.KeyData(
          timeStamp: timeStamp,
          type: ui.KeyEventType.up,
          physical: physicalKey,
          logical: logicalKey,
          character: null,
          synthesized: true,
        ),
        () {
          _pressingRecords.remove(physicalKey);
        }
      );
      type = ui.KeyEventType.down;

    } else if (isPhysicalDown) {
      // Case 2: Handle key down of normal keys
      if (_pressingRecords[physicalKey] != null) {
        // This physical key is being pressed according to the record.
        if (event.repeat ?? false) {
          // A normal repeated key.
          type = ui.KeyEventType.repeat;
        } else {
          // A non-repeated key has been pressed that has the exact physical key as
          // a currently pressed one. This can mean one of the following cases:
          //
          //  * Multiple keyboards are pressing keys with the same physical key.
          //  * The up event was lost during a loss of focus.
          //  * The previous down event was a system shortcut and its release
          //    was skipped (see `_startGuardingKey`,) such as holding Ctrl and
          //    pressing V then V, within the "guard window".
          //
          // The three cases can't be distinguished, and in the 3rd case, the
          // latter event must be dispatched as down events for the framework to
          // correctly recognize and choose to not to handle. Therefore, an up
          // event is synthesized before it.
          _dispatchKeyData!(ui.KeyData(
            timeStamp: timeStamp,
            type: ui.KeyEventType.up,
            physical: physicalKey,
            logical: logicalKey,
            character: null,
            synthesized: true,
          ));
          _pressingRecords.remove(physicalKey);
          type = ui.KeyEventType.down;
        }
      } else {
        // This physical key is not being pressed according to the record. It's a
        // normal down event, whether the system event is a repeat or not.
        type = ui.KeyEventType.down;
      }

    } else { // isPhysicalDown is false and not CapsLock
      // Case 2: Handle key up of normal keys
      if (_pressingRecords[physicalKey] == null) {
        // The physical key has been released before. It indicates multiple
        // keyboards pressed keys with the same physical key. Ignore the up event.
        event.preventDefault();
        return;
      }

      type = ui.KeyEventType.up;
    }

    // The _pressingRecords[physicalKey] might have been changed during the last
    // `if` clause.
    final int? lastLogicalRecord = _pressingRecords[physicalKey];

    final int? nextLogicalRecord;
    switch (type) {
      case ui.KeyEventType.down:
        assert(lastLogicalRecord == null);
        nextLogicalRecord = logicalKey;
        break;
      case ui.KeyEventType.up:
        assert(lastLogicalRecord != null);
        nextLogicalRecord = null;
        break;
      case ui.KeyEventType.repeat:
        assert(lastLogicalRecord != null);
        nextLogicalRecord = lastLogicalRecord;
        break;
    }
    if (nextLogicalRecord == null) {
      _pressingRecords.remove(physicalKey);
    } else {
      _pressingRecords[physicalKey] = nextLogicalRecord;
    }

    // After updating _pressingRecords, synchronize modifier states. The
    // `event.***Key` fields can be used to reduce some omitted modifier key
    // events. We can synthesize key up events if they are false. Key down
    // events can not be synthesized since we don't know which physical key they
    // represent.
    _kLogicalKeyToModifierGetter.forEach((int testeeLogicalKey, _ModifierGetter getModifier) {
      // Do not synthesize for the key of the current event. The event is the
      // ground truth.
      if (logicalKey == testeeLogicalKey) {
        return;
      }
      if (_pressingRecords.containsValue(testeeLogicalKey) && !getModifier(event)) {
        _pressingRecords.removeWhere((int physicalKey, int logicalRecord) {
          if (logicalRecord != testeeLogicalKey) {
            return false;
          }

          _dispatchKeyData!(ui.KeyData(
            timeStamp: timeStamp,
            type: ui.KeyEventType.up,
            physical: physicalKey,
            logical: testeeLogicalKey,
            character: null,
            synthesized: true,
          ));

          return true;
        });
      }
    });

    // Update key guards
    if (logicalKeyIsCharacter) {
      if (nextLogicalRecord != null) {
        _startGuardingKey(physicalKey, logicalKey, timeStamp);
      } else {
        _stopGuardingKey(physicalKey);
      }
    }

    final ui.KeyData keyData = ui.KeyData(
      timeStamp: timeStamp,
      type: type,
      physical: physicalKey,
      logical: lastLogicalRecord ?? logicalKey,
      character: type == ui.KeyEventType.up ? null : character,
      synthesized: false,
    );

    final bool primaryHandled = _dispatchKeyData!(keyData);
    if (primaryHandled) {
      event.preventDefault();
    }
  }

  // Parse the HTML event, update states, and dispatch Flutter key data through
  // [performDispatchKeyData].
  //
  //  * The method might dispatch some synthesized key data first to update states,
  //    results discarded.
  //  * Then it dispatches exactly one non-synthesized key data that corresponds
  //    to the `event`, i.e. the primary key data. If this dispatching returns
  //    true, then this event will be invoked `preventDefault`.
  //  * Some key data might be synthesized to update states after the main key
  //    data. They are always scheduled asynchronously with results discarded.
  void handleEvent(FlutterHtmlKeyboardEvent event) {
    assert(_dispatchKeyData == null);
    bool sentAnyEvents = false;
    _dispatchKeyData = (ui.KeyData data) {
      sentAnyEvents = true;
      return performDispatchKeyData(data);
    };
    try {
      _handleEvent(event);
    } finally {
      if (!sentAnyEvents) {
        _dispatchKeyData!(_emptyKeyData);
      }
      _dispatchKeyData = null;
    }
  }

  // Synthesize modifier keys up or down events only when the known pressing states are different.
  void synthesizeModifiersIfNeeded(
    bool altPressed,
    bool controlPressed,
    bool metaPressed,
    bool shiftPressed,
    num eventTimestamp,
  ) {
    _synthesizeModifierIfNeeded(
      _kPhysicalAltLeft,
      _kPhysicalAltRight,
      _kLogicalAltLeft,
      _kLogicalAltRight,
      altPressed ? ui.KeyEventType.down : ui.KeyEventType.up,
      eventTimestamp,
    );
    _synthesizeModifierIfNeeded(
      _kPhysicalControlLeft,
      _kPhysicalControlRight,
      _kLogicalControlLeft,
      _kLogicalControlRight,
      controlPressed ? ui.KeyEventType.down : ui.KeyEventType.up,
      eventTimestamp,
    );
    _synthesizeModifierIfNeeded(
      _kPhysicalMetaLeft,
      _kPhysicalMetaRight,
      _kLogicalMetaLeft,
      _kLogicalMetaRight,
      metaPressed ? ui.KeyEventType.down : ui.KeyEventType.up,
      eventTimestamp,
    );
    _synthesizeModifierIfNeeded(
      _kPhysicalShiftLeft,
      _kPhysicalShiftRight,
      _kLogicalShiftLeft,
      _kLogicalShiftRight,
      shiftPressed ? ui.KeyEventType.down : ui.KeyEventType.up,
      eventTimestamp,
    );
  }

  void _synthesizeModifierIfNeeded(
    int physicalLeft,
    int physicalRight,
    int logicalLeft,
    int logicalRight,
    ui.KeyEventType type,
    num domTimestamp,
  ) {
    final bool leftPressed = _pressingRecords.containsKey(physicalLeft);
    final bool rightPressed = _pressingRecords.containsKey(physicalRight);
    final bool alreadyPressed = leftPressed || rightPressed;
    final bool synthesizeDown = type == ui.KeyEventType.down && !alreadyPressed;
    final bool synthesizeUp = type == ui.KeyEventType.up && alreadyPressed;

    // Synthesize a down event only for the left key if right and left are not pressed
    if (synthesizeDown) {
      _synthesizeKeyDownEvent(domTimestamp, physicalLeft, logicalLeft);
    }

    // Synthesize an up event for left key if pressed
    if (synthesizeUp && leftPressed) {
      _synthesizeKeyUpEvent(domTimestamp, physicalLeft, logicalLeft);
    }

    // Synthesize an up event for right key if pressed
    if (synthesizeUp && rightPressed) {
      _synthesizeKeyUpEvent(domTimestamp, physicalRight, logicalRight);
    }
  }

  void _synthesizeKeyDownEvent(num domTimestamp, int physical, int logical) {
    performDispatchKeyData(ui.KeyData(
      timeStamp: _eventTimeStampToDuration(domTimestamp),
      type: ui.KeyEventType.down,
      physical: physical,
      logical: logical,
      character: null,
      synthesized: true,
    ));
    // Update pressing state
    _pressingRecords[physical] = logical;
  }

  void _synthesizeKeyUpEvent(num domTimestamp, int physical, int logical) {
    performDispatchKeyData(ui.KeyData(
      timeStamp: _eventTimeStampToDuration(domTimestamp),
      type: ui.KeyEventType.up,
      physical: physical,
      logical: logical,
      character: null,
      synthesized: true,
    ));
    // Update pressing states
    _pressingRecords.remove(physical);
  }

  @visibleForTesting
  bool debugKeyIsPressed(int physical) {
    return _pressingRecords.containsKey(physical);
  }
}
