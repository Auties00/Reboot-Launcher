enum InjectableDll {
  console,
  auth,
  gameServer,
  memoryLeak
}

extension InjectableDllVersionAware on InjectableDll {
  bool get isVersionDependent => this == InjectableDll.gameServer;
}
