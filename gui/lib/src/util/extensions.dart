extension IterableExtension<E> on Iterable<E> {
  E? firstWhereOrNull(bool test(E element)) {
    for (E element in this) {
      if (test(element)) return element;
    }
    return null;
  }
}

extension StringExtension on String {
  String? after(String leading) {
    final index = indexOf(leading);
    if(index == -1) {
      return null;
    }

    return substring(index + leading.length);
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