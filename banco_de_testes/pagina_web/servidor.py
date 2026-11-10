import http.server, json, sys, urllib.parse
OUT='/home/claude/mangue/host/out'
estado={'atual':'mangue'}; posts=[]
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self,*a): pass
    def env(self,code,ctype,body):
        b=body.encode(); self.send_response(code); self.send_header('Content-Type',ctype); self.send_header('Content-Length',str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        u=urllib.parse.urlparse(self.path); q=urllib.parse.parse_qs(u.query)
        if u.path=='/':
            if 'estado' in q: estado['atual']=q['estado'][0]
            return self.env(200,'text/html; charset=utf-8',open('/tmp/pagina.html').read())
        if estado['atual']=='offline' and u.path.startswith('/api/'): return self.env(503,'text/plain','')
        if u.path=='/api/saude':
            import os
            f=f"{OUT}/web_saude_{estado['atual']}.json"
            if not os.path.exists(f): f=f"{OUT}/saude.json"
            return self.env(200,'application/json',open(f).read())
        if u.path=='/api/estado': return self.env(200,'application/json',open(f"{OUT}/web_estado_{estado['atual']}.json").read())
        if u.path=='/api/matriz': return self.env(200,'text/plain',open(f"{OUT}/web_matriz_{estado['atual']}.txt").read())
        if u.path=='/modo':
            estado['atual']=q['e'][0]; return self.env(200,'text/plain','ok')
        if u.path=='/posts': return self.env(200,'application/json',json.dumps(posts))
        self.env(404,'text/plain','404')
    def do_POST(self):
        u=urllib.parse.urlparse(self.path); q=urllib.parse.parse_qs(u.query); posts.append(self.path)
        if u.path=='/api/paleta': estado['atual']=q['nome'][0]
        if u.path=='/api/orientacao':
            p=q.get('passo',[''])[0]
            if p=='1': return self.env(200,'application/json','{"ok":true,"estado":"passo2","msg":"Agora rode"}')
            if p=='2': return self.env(200,'application/json','{"ok":true,"estado":"concluido","msg":"Calibrado: sensor a 90 graus (desvio 0 graus)."}')
            return self.env(200,'application/json','{"ok":true,"estado":"reposto","msg":"ok"}')
        self.env(200,'application/json','{"ok":true}')
http.server.ThreadingHTTPServer(('127.0.0.1',8765),H).serve_forever()
