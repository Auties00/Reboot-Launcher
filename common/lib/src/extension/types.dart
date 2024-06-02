extension StringExtension on String {
  bool get isBlank {
    if(isEmpty) {
      return true;
    }

    for(var char in this.split("")) {
      if(char != " ") {
        return false;
      }
    }

    return true;
  }
}