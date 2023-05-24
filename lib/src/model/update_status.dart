enum UpdateStatus {
  waiting,
  started,
  success,
  error;

  bool isDone() => this == UpdateStatus.success || this == UpdateStatus.error;
}