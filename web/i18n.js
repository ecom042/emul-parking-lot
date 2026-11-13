"use strict";
// Interface language. English strings are the keys; Portuguese is the default
// and a missing entry falls back to English. Raw equipment values, plates,
// UART bytes and free-form firmware/server messages are never translated.
const LANGS = ["pt", "en"];
const LANG_KEY = "parking-lang";

const PT = {
  // Page and header
  "Parking lab": "Laboratório de estacionamento",
  "Language": "Idioma",
  "Run": "Execução",
  "Load": "Carregar",
  "Load the selected run and reset the machine": "Carrega a execução selecionada e reinicia a máquina",
  "Run controls": "Controles da execução",
  "Start": "Iniciar",
  "Start or resume (Space)": "Inicia ou retoma (Espaço)",
  "Pause": "Pausar",
  "Pause (Space)": "Pausa (Espaço)",
  "Reset": "Reiniciar",
  "Reset the machine and requeue every car (R)": "Reinicia a máquina e recoloca todos os carros na fila (R)",
  "Not started": "Não iniciado",
  "virtual": "virtual",
  "Connecting…": "Conectando…",
  "Connected": "Conectado",
  "Reconnecting. The view may be out of date.": "Reconectando. A tela pode estar desatualizada.",
  "Not connected. Open the link the launcher printed.": "Sem conexão. Abra o link que o launcher imprimiu.",
  "Failed": "Falhou",
  "Completed": "Concluído",
  "Running": "Rodando",
  "Paused": "Pausado",
  "Car queue": "Fila de carros",
  "Scenario: {name}": "Cenário: {name}",

  // Street, lanes and lot
  "Street": "Rua",
  "Add car": "Adicionar carro",
  "Add a car to the end of the queue (A)": "Adiciona um carro ao fim da fila (A)",
  "Cars wait here for the entry barrier. Select a car to change how it behaves.":
    "Os carros esperam aqui pela cancela de entrada. Selecione um carro para mudar o comportamento dele.",
  "The street is empty. Add a car to send it to the entry.":
    "A rua está vazia. Adicione um carro para mandá-lo à entrada.",
  "This scenario scripts its own vehicles. Load Car queue to drive cars yourself.":
    "Este cenário controla os próprios veículos. Carregue Fila de carros para conduzir os carros você mesmo.",
  "Lanes and lot": "Faixas e estacionamento",
  "Entry": "Entrada",
  "Exit": "Saída",
  "entry": "entrada",
  "exit": "saída",
  "Barrier": "Cancela",
  "Motor": "Motor",
  "Vehicle": "Veículo",
  "Beam": "Feixe",
  "barrier": "cancela",
  "off": "desligado",
  "opening": "abrindo",
  "closing": "fechando",
  "clear": "livre",
  "blocked": "bloqueado",
  "Reset required: {why}": "Reinício necessário: {why}",
  "Barrier jammed": "Cancela travada",
  "Limit switch fault injected": "Falha de fim de curso injetada",
  "Entry lane: car position, barrier angle, beam, signals and buzzer from equipment snapshots.":
    "Faixa de entrada: posição do carro, ângulo da cancela, feixe, sinais e buzzer vindos do equipamento.",
  "Exit lane with the payment terminal: car position, barrier angle, beam, signals and buzzer.":
    "Faixa de saída com o terminal de pagamento: posição do carro, ângulo da cancela, feixe, sinais e buzzer.",
  "Lot": "Estacionamento",
  "0 of 4 spots taken": "0 de 4 vagas ocupadas",
  "{n} of {total} spots taken": "{n} de {total} vagas ocupadas",
  "Scripted scenario": "Cenário roteirizado",
  "Spot {n}": "Vaga {n}",
  "Free": "Livre",
  "Leaves in {s} s": "Sai em {s} s",
  "Exit refused. Select to retry.": "Saída recusada. Selecione para tentar de novo.",
  "Parks {s} s": "Estaciona {s} s",

  // Display and verdicts
  "Exit terminal display: actual ST7789V pixels written over SPI":
    "Display do terminal de saída: pixels reais do ST7789V escritos via SPI",
  "Exit terminal display, from real SPI writes. Simulated payment, not PIX.":
    "Display do terminal de saída, a partir de escritas SPI reais. Pagamento simulado, não é PIX.",
  "Verdicts": "Veredictos",
  "No cars served yet": "Nenhum carro atendido ainda",
  "{pass} of {served} passed": "{pass} de {served} aprovados",
  "{n} checkpoints passed": "{n} pontos de verificação aprovados",
  "Checkpoint at {s} s on the {lane} lane": "Ponto de verificação em {s} s na faixa de {lane}",
  "{plate} at the {lane}: {outcome}": "{plate} na {lane}: {outcome}",
  "Expected: {expected}. {reason}": "Esperado: {expected}. {reason}",
  " or ": " ou ",

  // Car profile summaries and verdict outcomes
  "Low confidence, then read": "Baixa confiança, depois lida",
  "Plate never readable": "Placa nunca legível",
  "Entry camera silent": "Câmera de entrada muda",
  "Different plate read at exit": "Outra placa lida na saída",
  "Pays slowly": "Paga devagar",
  "Payment cancelled": "Pagamento cancelado",
  "Broken terminal reply": "Resposta do terminal corrompida",
  "Terminal silent": "Terminal mudo",
  "Hesitates": "Hesita",
  "Drives away while paying": "Vai embora durante o pagamento",
  "Stops under the beam": "Para sob o feixe",
  "Blocks the closing barrier": "Bloqueia a cancela fechando",
  "opened for entry": "abriu para a entrada",
  "opened for exit": "abriu para a saída",
  "refused": "recusou",
  "gave no assistance": "não pediu assistência",
  "let the car drive away": "deixou o carro ir embora",
  "requested assistance": "pediu assistência",
  "opened after the car left": "abriu depois que o carro foi embora",
  "in the street": "na rua",
  "at the entry": "na entrada",
  "parked": "estacionado",
  "waiting for the exit": "aguardando a saída",
  "at the exit": "na saída",
  "exit refused": "saída recusada",
  "left the lot": "saiu do estacionamento",
  "turned away": "recusado",

  // Errors and notices
  "The firmware broke a lab rule: {message}. Reset to run again.":
    "O firmware violou uma regra do laboratório: {message}. Reinicie para rodar de novo.",
  "The simulator stopped: {message}. Reset, or restart the launcher.":
    "O simulador parou: {message}. Reinicie ou abra o launcher de novo.",
  "An equipment action could not run at tick {tick}: the scene was not ready for it (for example, Next car needs the previous car past a closed barrier).":
    "Uma ação de equipamento não pôde rodar no tick {tick}: a cena não estava pronta para ela (por exemplo, Próximo carro exige que o carro anterior já tenha passado por uma cancela fechada).",
  "Invalid response from local bridge": "Resposta inválida da ponte local",
  "Request failed: HTTP {status}": "A requisição falhou: HTTP {status}",
  "Session authentication failed": "Falha na autenticação da sessão",
  "Source window gap; state resynchronized. UART history incomplete.":
    "Lacuna na janela da fonte; estado ressincronizado. Histórico da UART incompleto.",
  "Recent UART suffix only; current state snapshot remains authoritative.":
    "Só o trecho recente da UART; o estado atual continua sendo a referência.",

  // Diagnostics drawer and manual equipment
  "Diagnostics and manual equipment": "Diagnóstico e equipamento manual",
  "Diagnostics": "Diagnóstico",
  "Manual equipment": "Equipamento manual",
  "Events": "Eventos",
  "State": "Estado",
  "Commands and checkpoints": "Comandos e pontos de verificação",
  "These actions instruct equipment directly, never the motor. They can change a car's outcome or a scenario's verdict.":
    "Estas ações comandam o equipamento diretamente, nunca o motor. Elas podem mudar o resultado de um carro ou o veredito de um cenário.",
  "Lane": "Faixa",
  "Drive forward": "Avançar",
  "Stop car": "Parar carro",
  "Next car": "Próximo carro",
  "Repeat terminal reply": "Repetir resposta do terminal",
  "Cancel payment": "Cancelar pagamento",
  "Beam blocked": "Feixe bloqueado",
  "Limit switches": "Fins de curso",
  "Normal": "Normal",
  "Upper missing": "Superior ausente",
  "Lower missing": "Inferior ausente",
  "Both on": "Ambos acionados",
  "ADC noise": "Ruído do ADC",
  "Off": "Desligado",
  "±16 codes": "±16 códigos",
  "±64 codes": "±64 códigos",
  "Bytes on the camera and payment UARTs, as the equipment saw them. Firmware console logs are not shown here.":
    "Bytes nas UARTs da câmera e do pagamento, como o equipamento os viu. Os logs do console do firmware não aparecem aqui.",

  // Car editor
  "Car behaviour": "Comportamento do carro",
  "Close": "Fechar",
  "The exit was refused, so this car keeps its spot. Try the exit again:":
    "A saída foi recusada, então este carro continua na vaga. Tente a saída de novo:",
  "Pay normally": "Pagar normalmente",
  "Pay slowly": "Pagar devagar",
  "Cancel again": "Cancelar de novo",
  "Broken reply": "Resposta corrompida",
  "Plate": "Placa",
  "Entry camera": "Câmera de entrada",
  "Plate is read first time": "Placa lida na primeira tentativa",
  "Plate is never readable": "Placa nunca é legível",
  "Camera does not answer": "Câmera não responde",
  "Exit camera": "Câmera de saída",
  "Plate is read": "Placa é lida",
  "A different plate is read": "Outra placa é lida",
  "Terminal": "Terminal",
  "Pays": "Paga",
  "Pays slowly (1.5 s reply)": "Paga devagar (resposta em 1,5 s)",
  "Payment is cancelled": "Pagamento é cancelado",
  "Terminal sends a broken reply": "Terminal envia uma resposta corrompida",
  "Terminal does not answer": "Terminal não responde",
  "Driver": "Motorista",
  "Drives normally": "Dirige normalmente",
  "Hesitates on the approach": "Hesita na aproximação",
  "Stops under the entry beam": "Para sob o feixe de entrada",
  "Steps into the closing entry barrier": "Entra na cancela de entrada enquanto ela fecha",
  "Parks for": "Estaciona por",
  "Save": "Salvar",
  "Remove car": "Remover carro",
  "Waiting in the street. Every setting can change.": "Esperando na rua. Todas as opções podem mudar.",
  "Its exit was refused. Only its exit can change.": "A saída foi recusada. Só a saída pode mudar.",
  "Parked. Only its exit can change.": "Estacionado. Só a saída pode mudar.",
  "This car is {where}. Its settings are fixed now.": "Situação do carro: {where}. As opções agora estão fixas.",
  "Keyboard: Space starts or pauses, R resets, A adds a car, Esc closes the editor.":
    "Teclado: Espaço inicia ou pausa, R reinicia, A adiciona um carro, Esc fecha o editor.",
};

// Fixed phrases the lot driver (scripts/scenario/driver.py) sends as verdict
// reasons and lane halt notes. Anything else is shown as sent.
const SERVER_PT = {
  "drove away during payment": "foi embora durante o pagamento",
  "assistance requested": "assistência solicitada",
  "no opening before deadline": "não abriu antes do prazo",
  "car held the beam beyond the passage budget": "o carro ficou no feixe além do prazo de passagem",
  "obstruction while closing": "obstrução durante o fechamento",
  "barrier opened after the car drove away": "a cancela abriu depois que o carro foi embora",
  "barrier did not close; reset required": "a cancela não fechou; reinício necessário",
  "obstruction stopped the barrier; reset required": "a obstrução parou a cancela; reinício necessário",
};

function storedLang() {
  try {
    const value = localStorage.getItem(LANG_KEY);
    return LANGS.includes(value) ? value : "pt";
  } catch (_) {
    return "pt";
  }
}

let lang = storedLang();
const languageListeners = [];

function t(key, vars) {
  let value = (lang === "pt" && Object.hasOwn(PT, key)) ? PT[key] : key;
  if (vars) value = value.replace(/\{(\w+)\}/g, (match, name) => name in vars ? String(vars[name]) : match);
  return value;
}

function tServer(value) {
  if (lang !== "pt" || !value) return value;
  if (Object.hasOwn(SERVER_PT, value)) return SERVER_PT[value];
  const opened = /^opened for (.+)$/.exec(value);
  return opened ? `abriu para ${opened[1]}` : value;
}

// Static markup keeps its English text; the original is cached in the
// element's dataset so switching back and forth never loses the key.
function applyStatic() {
  if (typeof document === "undefined" || !document.documentElement) return;
  document.documentElement.lang = lang === "pt" ? "pt-BR" : "en";
  document.querySelectorAll("[data-i18n]").forEach(node => {
    if (!node.dataset.i18n) node.dataset.i18n = node.textContent.trim();
    node.textContent = t(node.dataset.i18n);
  });
  for (const [attr, slot] of [["title", "i18nTitle"], ["aria-label", "i18nLabel"]]) {
    document.querySelectorAll(`[${attr}]`).forEach(node => {
      if (node.dataset[slot] === undefined) node.dataset[slot] = node.getAttribute(attr);
      node.setAttribute(attr, t(node.dataset[slot]));
    });
  }
  document.querySelectorAll("[data-lang]").forEach(button => {
    button.setAttribute("aria-pressed", String(button.dataset.lang === lang));
  });
}

function onLanguage(listener) {
  languageListeners.push(listener);
}

function setLang(next) {
  if (!LANGS.includes(next) || next === lang) return;
  lang = next;
  try { localStorage.setItem(LANG_KEY, next); } catch (_) { /* private mode: not saved */ }
  applyStatic();
  for (const listener of languageListeners) listener(lang);
}

applyStatic();
if (typeof document !== "undefined") {
  document.querySelectorAll("[data-lang]").forEach(button => {
    button.onclick = () => setLang(button.dataset.lang);
  });
}
