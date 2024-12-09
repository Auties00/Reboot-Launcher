enum InjectableDll {
  console,
  starfall,
  reboot,
}

extension InjectableDllVersionAware on InjectableDll {
  bool get isVersionDependent => this == InjectableDll.reboot;
}
