extension IterableExtension<E> on Iterable<E> {
  E? firstWhereOrNull(bool test(E element)) {
    for (final element in this) {
      if (test(element)) {
        return element;
      }
    }
    return null;
  }
}

extension FutureExtension<T> on Future<T> {
  Future<T> withMinimumDuration(Duration duration) async {
    final result = await Future.wait([
      Future.delayed(duration),
      this
    ]);
    return result.last;
  }
}