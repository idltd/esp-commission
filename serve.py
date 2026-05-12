"""
HTTPS dev server for the PWA — self-signed cert generated in pure Python.
Camera access (getUserMedia) requires HTTPS on all mobile browsers.
"""
import http.server, ssl, os, socket, tempfile, datetime

try:
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
except ImportError:
    import subprocess, sys
    subprocess.check_call([sys.executable, "-m", "pip", "install", "cryptography"])
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa

PORT = 8443
PWA  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pwa")

def local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()

def make_cert(cert_path, key_path):
    key  = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "espcommission")])
    now  = datetime.datetime.utcnow()
    cert = (
        x509.CertificateBuilder()
        .subject_name(name).issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + datetime.timedelta(days=365))
        .sign(key, hashes.SHA256())
    )
    with open(key_path,  "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM,
                                  serialization.PrivateFormat.TraditionalOpenSSL,
                                  serialization.NoEncryption()))
    with open(cert_path, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))

class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(fmt % args)

os.chdir(PWA)
ip = local_ip()

tmp  = tempfile.mkdtemp()
cert = os.path.join(tmp, "cert.pem")
key  = os.path.join(tmp, "key.pem")
make_cert(cert, key)

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(cert, key)

httpd = http.server.HTTPServer(("", PORT), Handler)
httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)

print(f"\n  PWA on phone:  https://{ip}:{PORT}/")
print(f"  (accept the self-signed cert warning)\n")
httpd.serve_forever()
