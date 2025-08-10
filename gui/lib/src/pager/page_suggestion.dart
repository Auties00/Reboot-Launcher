class PageSuggestion {
  final String name;
  final String description;
  final String? content;
  final int pageIndex;
  final String? routeName;

  PageSuggestion({
    required this.name,
        required this.description,
        this.content,
        required this.pageIndex,
        this.routeName
  });
}
