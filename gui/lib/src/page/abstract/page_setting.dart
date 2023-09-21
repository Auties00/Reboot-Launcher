class PageSetting {
  final String name;
  final String description;
  final String? content;
  final List<PageSetting>? children;
  final int pageIndex;

  PageSetting(
      {required this.name,
      required this.description,
      this.content,
      this.children,
      this.pageIndex = -1});

  PageSetting withPageIndex(int pageIndex) => this.pageIndex != -1
      ? this
      : PageSetting(
          name: name,
          description: description,
          content: content,
          children: children,
          pageIndex: pageIndex);

  @override
  String toString() => "$name: $description";
}
