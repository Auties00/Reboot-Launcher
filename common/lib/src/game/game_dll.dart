enum GameDll {
  console,
  auth,
  gameServer,
  memoryLeak
}

extension InjectableDllVersionAware on GameDll {
  bool get isVersionDependent => this == GameDll.gameServer;
}
