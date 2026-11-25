#!/usr/bin/env python3
# fake_escl.py PORT CAPS.xml PAGE.png -- a stand-in eSCL scanner for testing
# Scanit without the ET-8550. Serves the capabilities, takes a job (201 +
# Location), answers NextDocument with one 503 ("calibrating") then the page,
# then 404; DELETE cancels. The last ScanSettings POSTed lands in
# /tmp/last_settings.xml.
#
#   python3 tools/fake_escl.py 8099 tools/fake_caps.xml some_page.png &
#   SCANIT_HOST=127.0.0.1:8099 ./build/scanit
import http.server, sys, time, re
CAPS = open(sys.argv[2]).read()
PAGE = open(sys.argv[3], 'rb').read()
jobs = {}
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): sys.stderr.write("fake: " + (a[0] % a[1:]) + "\n")
    def send(self, code, body=b"", ctype="text/xml", extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers()
        if body: self.wfile.write(body)
    def do_GET(self):
        if self.path == "/eSCL/ScannerCapabilities": return self.send(200, CAPS.encode())
        if self.path == "/eSCL/ScannerStatus": return self.send(200, b"<scan:ScannerStatus><pwg:State>Idle</pwg:State></scan:ScannerStatus>")
        m = re.match(r"/eSCL/ScanJobs/(\w+)/NextDocument", self.path)
        if m and m.group(1) in jobs:
            j = jobs[m.group(1)]
            if j["served"]: return self.send(404)
            if j["polls"] < 1:   # one 503 while "calibrating"
                j["polls"] += 1; return self.send(503)
            time.sleep(1.5)
            j["served"] = True
            return self.send(200, PAGE, "image/png")
        self.send(404)
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = self.rfile.read(n).decode()
        open("/tmp/last_settings.xml", "w").write(body)
        jid = str(len(jobs) + 1); jobs[jid] = {"served": False, "polls": 0}
        self.send(201, b"", extra={"Location": "/eSCL/ScanJobs/" + jid})
    def do_DELETE(self):
        self.send(200)
http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
