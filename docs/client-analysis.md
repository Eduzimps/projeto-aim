# Análise do cliente recebido

## Identidade e integridade

As cinco partes recebidas foram conferidas pelos hashes SHA256 do manifesto e pelos CRCs dos ZIPs. A recomposição produziu exatamente o hash e o tamanho declarados.

| Propriedade | Resultado |
| --- | --- |
| Arquivo | `BloodStrike.exe` |
| SHA256 | `d1a6589bfe38da8f316b577cf8dc861e2aa87693b3a6c3846a0e1555540e9535` |
| Tamanho em disco | 141.431.288 bytes |
| Formato | PE32+, AMD64 |
| Base preferencial | `0x140000000` |
| Tamanho da imagem | `0x094E9000` |
| Evidência de engine | Namespace, RTTI e interfaces Messiah; identificador NetEase Messiah Shader Compiler |
| Evidência de runtime | Seções `PyRuntim` e `AsyncioD`, bindings Python |

O executável proprietário recebido permanece fora do checkout e não é distribuído neste repositório.

## Arquivos de offsets anteriores

Os seis endereços grandes do JSON anterior excedem o tamanho desta imagem e não podem ser RVAs deste executável. A lista também não identifica um módulo alternativo de origem. Portanto, esses valores não foram usados na integração.

## Candidatos derivados do código

Os valores abaixo foram rastreados em instruções reais e metadados de reflexão deste arquivo. São **candidatos estáticos**, sem confirmação em uma instância do jogo em execução.

| Acesso | Evidência |
| --- | --- |
| Singleton ClientEngine, RVA `0x080D5980` | Construtor em `0x003B9C70`, publicação em `0x003B9D63` |
| ClientEngine → gameplay, membro `0x58` | Setter em `0x003BAA70`, gravação em `0x003BAAFD` |
| IGameplay → ClientPlayer, membro `0x58` | Construtor em `0x008991E0` e getter refletido em `0x0092BEC0` |
| ClientPlayer → câmera, membro `0xD88` | Binding `ClientPlayer.Camera`; getter em `0x00D09ED0` |
| Câmera → visão, membro `0x41C` | Binding `ICamera.ViewMatrix`; getter em `0x00964090` |
| Câmera → FOV, membro `0x454` | Binding `ICamera.FieldOfView`; getter em `0x00964160` |
| Câmera → near/far, membros `0x458`/`0x45C` | Leituras em função `0x0314C250` |

A função em `0x0314C250` acessa a mesma cadeia de singleton/gameplay/player/câmera e lê visão, FOV e planos de recorte. Isso constitui uma confirmação estática independente dos getters e construtores.

O diagnóstico `zyon-camera-probe.exe` confere a identidade do arquivo, tamanho da imagem, vtables dos três objetos da cadeia e faixa da vtable da câmera antes de ler amostras limitadas. Os valores são classificados como candidatos: finitude e limites numéricos não demonstram, sozinhos, uma projeção correta na tela.

## Validação que falta

- Confirmar a cadeia e os valores da câmera na build recebida em execução.
- Confirmar tipo, convenções, dimensões e projeção da matriz de visão.
- Identificar a coleção de jogadores e separar entidades de cenário, aliados e inimigos.
- Identificar vida atual/máxima e estados de jogador. As ocorrências de health examinadas pertencem a objetos destrutíveis e não estabelecem a vida dos jogadores.
- Validar posições/ossos e correspondência entre pixels e orientação da câmera.

O diagnóstico de câmera é a próxima verificação no Windows. O painel continua sendo uma prévia com alvos sintéticos; ESP e aimbot no jogo ainda não foram demonstrados.
