class FortniteServer {
  final String id;
  final String name;
  final String description;
  final String author;
  final String ip;
  final String version;
  final String? password;
  final DateTime timestamp;
  final bool discoverable;

  FortniteServer({
    required this.id,
    required this.name,
    required this.description,
    required this.author,
    required this.ip,
    required this.version,
    required this.password,
    required this.timestamp,
    required this.discoverable
  });

  factory FortniteServer.fromJson(json) => FortniteServer(
      id: json["id"],
      name: json["name"],
      description: json["description"],
      author: json["author"],
      ip: json["ip"],
      version: json["version"],
      password: json["password"],
      timestamp: json.containsKey("json") ? DateTime.parse(json["timestamp"]) : DateTime.now(),
      discoverable: json["discoverable"] ?? false
  );

  Map<String, dynamic> toJson() => {
    "id": id,
    "name": name,
    "description": description,
    "author": author,
    "ip": ip,
    "version": version,
    "password": password,
    "timestamp": timestamp.toString(),
    "discoverable": discoverable
  };
}
