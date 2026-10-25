# Run by pyrun with the Python and PyQt6 of the AppDir: Qt's own networking (QSslSocket
# with the bundled OpenSSL, what the Nexus API and the self-updater use) must complete a
# TLS handshake and get an HTTP answer from an https host.
#
#   qt-https-check.py [url]
import sys

from PyQt6.QtCore import QCoreApplication, QTimer, QUrl
from PyQt6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest, QSslSocket

url = sys.argv[1] if len(sys.argv) > 1 else "https://api.github.com"
app = QCoreApplication(sys.argv)
print("qt https check: supportsSsl:",
      QSslSocket.supportsSsl(), "|", QSslSocket.sslLibraryVersionString(), flush=True)
if not QSslSocket.supportsSsl():
    print("qt https check FAILED: no SSL support", file=sys.stderr)
    sys.exit(1)

result = {"rc": 1}
manager = QNetworkAccessManager()
request = QNetworkRequest(QUrl(url))
request.setRawHeader(b"User-Agent", b"mo2-smoke")
reply = manager.get(request)


def finished():
    status = reply.attribute(QNetworkRequest.Attribute.HttpStatusCodeAttribute)
    ssl_errors = reply.sslConfiguration().peerCertificate()
    if reply.error() == QNetworkReply.NetworkError.NoError or status:
        print(f"qt https check: {url} -> HTTP {status}, peer {ssl_errors.subjectDisplayName()!r}",
              flush=True)
        result["rc"] = 0
    else:
        print(f"qt https check FAILED: {reply.error()} {reply.errorString()}", file=sys.stderr)
    app.quit()


def on_ssl_errors(errors):
    print("qt https check FAILED: TLS errors:", [e.errorString() for e in errors],
          file=sys.stderr)


reply.finished.connect(finished)
reply.sslErrors.connect(on_ssl_errors)
QTimer.singleShot(40000, lambda: (print("qt https check FAILED: timeout", file=sys.stderr),
                                  app.quit()))
app.exec()
sys.exit(result["rc"])
