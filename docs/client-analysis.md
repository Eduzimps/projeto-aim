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

Os valores abaixo foram rastreados em instruções reais e metadados de reflexão deste arquivo. O teste enviado pelo usuário depois produziu três amostras de câmera no treinamento; a identificação de entidades e a projeção na imagem ainda não foram validadas em execução.

| Acesso | Evidência |
| --- | --- |
| Singleton ClientEngine, RVA `0x080D5980` | Construtor em `0x003B9C70`, publicação em `0x003B9D63` |
| ClientEngine → gameplay, membro `0x58` | Setter em `0x003BAA70`, gravação em `0x003BAAFD` |
| IGameplay → ClientPlayer, membro `0x58` | Construtor em `0x008991E0` e getter refletido em `0x0092BEC0` |
| ClientPlayer → câmera, membro `0xD88` | Binding `ClientPlayer.Camera`; getter em `0x00D09ED0` |
| Câmera → visão, membro `0x41C` | Binding `ICamera.ViewMatrix`; getter em `0x00964090` |
| Câmera → FOV, membro `0x454` | Binding `ICamera.FieldOfView`; getter em `0x00964160` |
| Câmera → near/far, membros `0x458`/`0x45C` | Leituras em função `0x0314C250` |
| IGameplay → cenário, membro `0x60` | Getter refletido em `0x00269CD0`, setter em `0x009496B0` |
| ClientScenario → mundo ativo, membro `0xD60` | Getters em `0x00CFD750`/`0x00CFD990`, setter em `0x00CFD9A0` |

A função em `0x0314C250` acessa a mesma cadeia de singleton/gameplay/player/câmera e lê visão, FOV e planos de recorte. Isso constitui uma confirmação estática independente dos getters e construtores.

O diagnóstico `zyon-camera-probe.exe` confere a identidade do arquivo, tamanho da imagem, vtables dos três objetos da cadeia e faixa da vtable da câmera antes de ler amostras limitadas. Os valores são classificados como candidatos: finitude e limites numéricos não demonstram, sozinhos, uma projeção correta na tela.

## Resultado recebido do treinamento

O diagnóstico identificou o SHA256, o tamanho em disco e o tamanho da imagem esperados. As três amostras passaram nas verificações de vtables da cadeia e de faixa da vtable da câmera. O FOV relatado foi 75 graus, com planos de recorte de aproximadamente 0,1 e 3000. As amostras permaneceram iguais durante o intervalo de 0,8 segundo; não constituem um teste de acompanhamento de movimento.

O bloco de rotação tem eixos aproximadamente ortonormais. Isso é compatível com uma câmera, mas não demonstra, sozinho, sua correspondência com a imagem exibida. Coordenadas exatas fornecidas pelo usuário não são publicadas neste relatório.

O primeiro relatório de cena passou na identidade da build e na leitura da câmera, inclusive origem e viewport. Parou com `null_pointer` em `world_vtable`: o campo `camera.World` estava nulo. Foram feitas 21 leituras, sem erro Win32, e nenhuma lista de entidades foi acessada. Portanto, `objects_observed: 0` nesse relatório não indica cena vazia.

## Projeção derivada

O binding `ICamera.GetScreenPointFromWorldPoint` encaminha para a função em RVA `0x00275DB0`. Ela subtrai a origem em `camera + 0x7C` e multiplica pelas três colunas dos primeiros nove floats de `ViewMatrix`. A câmera olha para Z negativo. Os campos `uint16` em `+0x408` e `+0x40A` fornecem largura e altura; a altura escala os dois eixos com FOV vertical. A origem de tela fica no canto superior esquerdo, com Y para baixo.

`include/aim/messiah_projection.hpp` implementa essa matemática separadamente da projeção OpenGL da prévia. Usa precisão dupla e não reproduz o arredondamento do motor para décimos de pixel. Testes com pontos analíticos cobrem rotação, origem, FOV, proporção da tela, pontos atrás da câmera e entradas inválidas; isso não substitui conferir o resultado na imagem do jogo.

## Próxima verificação de cena

O formato v2 substitui a origem de mundo por `IGameplay.Scenario` (`+0x60`) → `ClientScenario.ActiveWorld` (`+0xD60`). Essa cadeia foi confirmada em getters independentes, setter, construtor e RTTI. Como a propriedade de cenário é genericamente `IComponent`, a vtable exata `0x04FA1CE8` é exigida antes de acessar `+0xD60`. O mundo exige a vtable `0x04ECA2E0`. O diagnóstico não depende mais de `camera.World`.

Os getters fornecem o nível padrão em `world + 0x148` e a área raiz em `level + 0xF0`. A coleção `ActiveLevels` usa sentinela em `world + 0x160` e valor de nó em `+0x10`; `SubAreas` usa sentinela em `level + 0x100` e valor em `+0x30`; `IArea.Entities` usa sentinela em `area + 0xB8` e valor em `+0x10`. Iteradores refletidos e construtores corroboram esses layouts. Somente links e ponteiros de valor são lidos; chaves e nomes das coleções não são coletados.

O diagnóstico percorre o nível padrão e os ativos, suas áreas raiz e subáreas imediatas, com deduplicação. Os limites são 16 níveis, 32 áreas e 256 nós de entidades globalmente, incluindo nós repetidos que apontam para o mesmo objeto. Todas as leituras compartilham o limite de 4.096 chamadas e 64 KiB solicitados; a CLI encerra novas leituras após uma janela de 2 segundos. Arestas de propriedade, nós e sentinelas são rechecados. Isso detecta algumas alterações, sem produzir um snapshot atômico.

Para tipos `IEntity` conhecidos com base no deslocamento zero, o helper de `GetEntityPosition` em RVA `0x008977B0` soma a translação float em `+0x7C` à translação inteira assinada em `+0x1AC`. Somente quando os três inteiros são `INT32_MAX` a parcela inteira é zerada. O relatório inclui até 16 amostras com posição e projeção matemática, priorizando objetos com esqueleto sem classificá-los como jogadores. Tipos desconhecidos recebem apenas contagens por RVA de vtable e nenhuma leitura de campos tipados. Endereços absolutos de objetos e nomes de jogadores não são emitidos.

Testes reproduzem `camera.World` nulo, níveis/áreas sem raiz, coleções sobrepostas, ciclos, mudanças durante a leitura, falhas de tipo, posições inválidas e limites globais. A nova cadeia ainda precisa ser testada no cliente em execução. As amostras privadas de posição não são publicadas neste repositório.

## Validação que falta

- Conferir o alinhamento da projeção com a imagem exibida; origem e dimensões já foram lidas no relatório recebido.
- Confirmar o caminho de cena em execução e sua cobertura dos objetos relevantes.
- Identificar a coleção de jogadores e separar entidades de cenário, aliados e inimigos.
- Identificar vida atual/máxima e estados de jogador. As ocorrências de health examinadas pertencem a objetos destrutíveis e não estabelecem a vida dos jogadores.
- Validar posições/ossos e correspondência entre pixels e orientação da câmera.

O diagnóstico de cena é a próxima verificação no Windows. O painel continua sendo uma prévia com alvos sintéticos; ESP e aimbot no jogo ainda não foram demonstrados.
