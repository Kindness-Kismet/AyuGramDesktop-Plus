#ifdef _DEBUG
#include "ayu/debug/debug_server.h"

#include "ayu/debug/debug_commands.h"
#include "ayu/debug/crash_dump.h"

#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <QCoreApplication>

namespace AyuDebug {
namespace {

// 固定端口便于外部脚本直接连接；只绑回环，不对外暴露。
constexpr auto kPort = 20100;

std::unique_ptr<QTcpServer> Server;

void Reply(not_null<QTcpSocket*> socket, const Result &result) {
	auto line = (result.ok ? u"OK"_q : u"ERR"_q);
	if (!result.payload.isEmpty()) {
		line += ' ' + result.payload;
	}
	line += '\n';
	socket->write(line.toUtf8());
	socket->flush();
	// 客户端读到 EOF 才认为响应结束，因此一条指令一条连接，回完即断。
	// disconnectFromHost 在回环上同步触发 disconnected，会在 readyRead handler
	// 还在栈上时就 delete buffer，造成 use-after-free。改用队列延迟到事件循环。
	QMetaObject::invokeMethod(socket, [socket] {
		socket->disconnectFromHost();
	}, Qt::QueuedConnection);
}

void HandleLine(not_null<QTcpSocket*> socket, const QString &line) {
	const auto trimmed = line.trimmed();
	if (trimmed.isEmpty()) {
		return;
	}
	// 参数按空白切分；需要含空格的值时用引号，见下面的 Split。
	auto parts = QStringList();
	auto current = QString();
	auto quoted = false;
	for (const auto ch : trimmed) {
		if (ch == '"') {
			quoted = !quoted;
		} else if (ch.isSpace() && !quoted) {
			if (!current.isEmpty()) {
				parts.push_back(current);
				current.clear();
			}
		} else {
			current += ch;
		}
	}
	if (!current.isEmpty()) {
		parts.push_back(current);
	}
	if (parts.isEmpty()) {
		return;
	}
	const auto command = parts.takeFirst();
	Reply(socket, Execute(command, parts));
}

void SetupConnection(not_null<QTcpSocket*> socket) {
	// 每条连接自带缓冲，指令可能分片到达，攒够一行再执行。
	const auto buffer = new QString();
	QObject::connect(socket, &QTcpSocket::readyRead, socket, [=] {
		buffer->append(QString::fromUtf8(socket->readAll()));
		while (true) {
			const auto at = buffer->indexOf('\n');
			if (at < 0) {
				break;
			}
			const auto line = buffer->left(at);
			buffer->remove(0, at + 1);
			HandleLine(socket, line);
		}
	});
	QObject::connect(socket, &QTcpSocket::disconnected, socket, [=] {
		delete buffer;
		socket->deleteLater();
	});
}

} // namespace

void StartServer() {
	if (Server) {
		return;
	}
	InstallCrashHandler();
	auto server = std::make_unique<QTcpServer>();
	if (!server->listen(QHostAddress::LocalHost, kPort)) {
		LOG(("Debug server: cannot listen on %1: %2"
			).arg(kPort
			).arg(server->errorString()));
		return;
	}
	const auto raw = server.get();
	QObject::connect(raw, &QTcpServer::newConnection, raw, [=] {
		while (const auto socket = raw->nextPendingConnection()) {
			SetupConnection(socket);
		}
	});
	Server = std::move(server);
	// Server 是静态持有，不挂清理会在 QApplication 析构后的静态析构期
	// 才销毁 QObject，触发 Debug CRT abort 弹窗。aboutToQuit 时应用仍在，
	// 是销毁 server 的最后安全窗口。
	QObject::connect(
		qApp,
		&QCoreApplication::aboutToQuit,
		qApp,
		[] { Server = nullptr; },
		Qt::UniqueConnection);
	LOG(("Debug server: listening on 127.0.0.1:%1").arg(kPort));
}

void StopServer() {
	Server = nullptr;
}

int ServerPort() {
	return Server ? kPort : 0;
}

} // namespace AyuDebug
#endif // _DEBUG
