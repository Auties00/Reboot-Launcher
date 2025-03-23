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