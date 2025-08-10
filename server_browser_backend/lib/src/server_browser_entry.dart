class ServerBrowserEntry {
  final String id;
  final String name;
  final String description;
  final String version;
  final String password;
  final DateTime timestamp;
  final String ip;
  final String author;

  ServerBrowserEntry({
    required this.id,
    required this.name,
    required this.description,
    required this.version,
    required this.password,
    required this.timestamp,
    required this.ip,
    required this.author
  });

  Map<String, dynamic> toJson() {
    return {
      'id': id,
      'name': name,
      'description': description,
      'version': version,
      'password': password,
      'timestamp': timestamp.toIso8601String(),
      'ip': ip,
      'author': author
    };
  }

  static ServerBrowserEntry fromJson(Map<String, dynamic> json) {
    return ServerBrowserEntry(
      id: json['id'],
      name: json['name'],
      description: json['description'],
      version: json['version'],
      password: json['password'],
      timestamp: DateTime.parse(json['timestamp']),
      ip: json['ip'],
      author: json['author']
    );
  }
}