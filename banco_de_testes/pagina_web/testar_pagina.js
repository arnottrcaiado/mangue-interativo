// Testa a página REAL do firmware num Chrome real: escolher paleta -> POST, tema, espelho, aviso.
const puppeteer = require('puppeteer-core');
const OUT = '/home/claude/mangue/preview';
let falhas = 0;
const verifica = (c, m) => { console.log(`  [${c ? 'OK ' : 'FALHA'}] ${m}`); if (!c) falhas++; };
const espera = ms => new Promise(r => setTimeout(r, ms));

(async () => {
  const b = await puppeteer.launch({ executablePath: '/usr/bin/google-chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage();
  await p.setViewport({ width: 520, height: 1180, deviceScaleFactor: 1 });
  const erros = []; p.on('pageerror', e => erros.push(e.message));
  await p.goto('http://127.0.0.1:8765/?estado=mangue'); await espera(2600);
  const ler = () => p.evaluate(() => {
    const cs = getComputedStyle(document.body), r = document.documentElement.style;
    const acesos = [...document.querySelectorAll('#espelho i')].filter(i => i.style.background && i.style.background !== 'rgb(21, 21, 21)').length;
    return { fundo: cs.backgroundColor, acento: r.getPropertyValue('--acento'), sel: document.querySelector('#paleta').value,
             amostra0: document.querySelector('#amostras div') && document.querySelector('#amostras div').style.background,
             acesos };
  });
  const m = await ler();
  verifica(erros.length === 0, 'Pagina carrega sem erros de JavaScript' + (erros.length ? ' -> ' + erros.join('; ') : ''));
  verifica(m.acesos > 10, `Espelho ao vivo desenha o rosto (${m.acesos} LEDs acesos)`);
  await p.screenshot({ path: `${OUT}/web_mangue.png` });

  const resultados = { mangue: m };
  for (const nome of ['oceano', 'por_do_sol', 'pastel', 'floresta']) {
    await p.select('#paleta', nome);              // como o utilizador: escolher no menu
    await espera(2600);                           // fetch + transição CSS de 1,2 s
    const r = await ler(); resultados[nome] = r;
    await p.screenshot({ path: `${OUT}/web_${nome}.png` });
  }
  const posts = await (await fetch('http://127.0.0.1:8765/posts')).json();
  verifica(['oceano', 'por_do_sol', 'pastel', 'floresta'].every(n => posts.includes('/api/paleta?nome=' + n)), 'Escolher no menu envia POST /api/paleta para cada paleta');
  const fundos = new Set(Object.values(resultados).map(r => r.fundo));
  verifica(fundos.size === 5, `Fundo da pagina muda com a paleta (${fundos.size} fundos diferentes em 5 paletas)`);
  verifica(new Set(Object.values(resultados).map(r => r.amostra0)).size === 5, 'Amostras mudam com a paleta');
  console.log('   fundos:', Object.entries(resultados).map(([k, v]) => k + '=' + v.fundo).join('  '));

  // Cor única com o seletor de cor
  await p.select('#paleta', 'cor_unica'); await espera(300);
  await p.evaluate(() => { const c = document.querySelector('#cor'); c.value = '#ff8800'; c.dispatchEvent(new Event('change')); });
  await espera(2600);
  const posts2 = await (await fetch('http://127.0.0.1:8765/posts')).json();
  verifica(posts2.some(x => x.startsWith('/api/paleta?nome=cor_unica&cor=ff8800')), 'Seletor de cor envia a cor unica escolhida');
  await p.screenshot({ path: `${OUT}/web_cor_unica.png` });

  // Saúde do sistema: tudo bem
  await p.goto('http://127.0.0.1:8765/?estado=oceano'); await espera(2600);
  const ok = await p.evaluate(() => ({ selo: document.querySelector('#selo').textContent, linhas: document.querySelectorAll('#saude .lin').length,
                                       hist: document.querySelectorAll('#eventos div').length }));
  verifica(ok.selo === 'Tudo bem' && ok.linhas === 4, `Painel de saude: selo "${ok.selo}", ${ok.linhas} componentes`);
  verifica(ok.hist > 0, `Historico mostra ${ok.hist} eventos`);
  // Mini-painel igual ao da placa + botão "Mostrar na placa"
  const pw = await p.evaluate(() => [...document.querySelectorAll('#painelWeb i')].filter(i => i.style.background && i.style.background !== 'rgb(21, 21, 21)').length);
  verifica(pw === 22, `Pagina desenha os mesmos 4 icones da placa (${pw} pixels: 6+5+7+4)`);
  await p.click('button[onclick="post(\'/api/painel\')"]'); await espera(300);
  const posts4 = await (await fetch('http://127.0.0.1:8765/posts')).json();
  verifica(posts4.includes('/api/painel'), 'Botao "Mostrar na placa" envia POST /api/painel');
  // Erro do acelerómetro: o quê + o que fazer
  await p.goto('http://127.0.0.1:8765/?estado=erro_imu'); await espera(2600);
  const er = await p.evaluate(() => ({ selo: document.querySelector('#selo').textContent, txt: document.querySelector('#saude').textContent }));
  verifica(er.selo === 'Erro' && /Acelerómetro.*Erro/.test(er.txt) && /I2C/.test(er.txt) && /Reinicie/.test(er.txt),
           'Erro do acelerometro: selo "Erro", componente identificado e acao sugerida');
  const pwErro = await p.evaluate(() => [...document.querySelectorAll('#painelWeb i')].some(i => i.style.background === 'rgb(255, 0, 0)'));
  verifica(pwErro, 'Mini-painel mostra o icone do acelerometro a vermelho');
  await p.evaluate(() => window.scrollTo(0, document.querySelector('#saude').getBoundingClientRect().top + window.scrollY - 140));
  await p.screenshot({ path: `${OUT}/web_saude_erro.png` });
  // Planta inacessível (ex.: perdeu o Wi-Fi): a página diz isso claramente
  await fetch('http://127.0.0.1:8765/modo?e=offline'); await espera(2800);
  const off = await p.$eval('#offline', x => getComputedStyle(x).display);
  verifica(off === 'block', 'Sem resposta da planta -> faixa "Sem ligacao" visivel');
  await fetch('http://127.0.0.1:8765/modo?e=oceano'); await espera(1500);
  const off2 = await p.$eval('#offline', x => getComputedStyle(x).display);
  verifica(off2 === 'none', 'Ligacao de volta -> faixa desaparece sozinha');
  // Orientação: interruptor de rotação automática
  const rotTxt = await p.$eval('#rotPos', x => x.textContent);
  const rotChk = await p.$eval('#btnAuto', x => x.classList.contains('on'));
  verifica(rotChk === true && /°/.test(rotTxt), `Cartao de orientacao mostra estado ("${rotTxt}", automatica destacada=${rotChk})`);
  await p.click('button[onclick="post(\'/api/girar?sentido=1\')"]'); await espera(300);
  await p.click('button[onclick="post(\'/api/girar?sentido=-1\')"]'); await espera(300);
  const pg = await (await fetch('http://127.0.0.1:8765/posts')).json();
  verifica(pg.includes('/api/girar?sentido=1') && pg.includes('/api/girar?sentido=-1'), 'Botoes ↺ ↻ enviam POST /api/girar');
  // Assistente de calibração (2 passos)
  await p.click('button[onclick="calibrarOri()"]'); await espera(200);
  const t1 = await p.$eval('#assistTxt', x => x.textContent);
  await p.click('#assistOk'); await espera(500);
  const t2 = await p.$eval('#assistTxt', x => x.textContent);
  await p.evaluate(() => document.querySelector('#assist').scrollIntoView());
  await p.screenshot({ path: `${OUT}/web_calibrar_orientacao.png` });
  await p.click('#assistOk'); await espera(500);
  const t3 = await p.$eval('#assistTxt', x => x.textContent);
  const posts6 = await (await fetch('http://127.0.0.1:8765/posts')).json();
  verifica(/Passo 1/.test(t1) && /Passo 2/.test(t2) && /Calibrado/.test(t3) &&
           posts6.includes('/api/orientacao?passo=1') && posts6.includes('/api/orientacao?passo=2'),
           'Assistente de calibracao: passo 1 -> passo 2 -> concluido');
  const eix = await p.$eval('#rotEix', x => x.textContent);
  verifica(/Montagem do sensor: 90°/.test(eix), `Pagina mostra a montagem do sensor ("${eix}")`);
  await p.click('#btnFixa'); await espera(400);
  const posts5 = await (await fetch('http://127.0.0.1:8765/posts')).json();
  verifica(posts5.includes('/api/rotacao?auto=0'), 'Botao "Fixa" envia POST /api/rotacao?auto=0');
  await p.evaluate(() => document.querySelector('#btnAuto').scrollIntoView());
  await p.evaluate(() => window.scrollBy(0, -60));
  await p.screenshot({ path: `${OUT}/web_orientacao.png` });
  // Cartão "Cores dos LEDs"
  const ordem = await p.$eval('#ordem', x => x.value);
  verifica(ordem === 'RGB', 'Seletor mostra a ordem de cor atual (RGB)');
  await p.select('#ordem', 'GRB'); await espera(400);
  await p.click('button[onclick="testarCores()"]'); await espera(400);
  const posts3 = await (await fetch('http://127.0.0.1:8765/posts')).json();
  verifica(posts3.includes('/api/ordem?nome=GRB') && posts3.includes('/api/teste_cor'), 'Pagina envia mudanca de ordem e pedido de teste de cores');
  await p.goto('http://127.0.0.1:8765/?estado=teste_verde'); await espera(1500);
  const txt = await p.$eval('#testeTxt', x => x.textContent);
  verifica(/VERDE/.test(txt), `Durante o teste a pagina diz a cor esperada ("${txt}")`);
  await p.evaluate(() => document.querySelector('#ordem').scrollIntoView());
  await p.screenshot({ path: `${OUT}/web_teste_cores.png` });
  await b.close();
  console.log(`\nResultado pagina: ${falhas} falha(s)`);
  process.exit(falhas ? 1 : 0);
})();
