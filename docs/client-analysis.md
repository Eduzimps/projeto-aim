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

O relatório seguinte, formato v2, acessou o mundo ativo do cenário e observou 256 objetos em duas áreas de dois níveis, com 35 esqueletos e leitura de posição para todos os objetos observados. Parou em `node_limit_reached`, com 2.177 leituras e 21.924 bytes solicitados, sem erro Win32. A coleta foi truncada e não executou a rechecagem final de propriedade; portanto não estabelece uma lista completa e estável de personagens. Os RVAs predominantes correspondem ao tipo genérico `IEntity`, que também representa objetos de cenário e peças anexas.

## Projeção derivada

O binding `ICamera.GetScreenPointFromWorldPoint` encaminha para a função em RVA `0x00275DB0`. Ela subtrai a origem em `camera + 0x7C` e multiplica pelas três colunas dos primeiros nove floats de `ViewMatrix`. A câmera olha para Z negativo. Os campos `uint16` em `+0x408` e `+0x40A` fornecem largura e altura; a altura escala os dois eixos com FOV vertical. A origem de tela fica no canto superior esquerdo, com Y para baixo.

`include/aim/messiah_projection.hpp` implementa essa matemática separadamente da projeção OpenGL da prévia. Usa precisão dupla e não reproduz o arredondamento do motor para décimos de pixel. Testes com pontos analíticos cobrem rotação, origem, FOV, proporção da tela, pontos atrás da câmera e entradas inválidas; isso não substitui conferir o resultado na imagem do jogo.

## Próxima verificação de cena

O formato v2 substitui a origem de mundo por `IGameplay.Scenario` (`+0x60`) → `ClientScenario.ActiveWorld` (`+0xD60`). Essa cadeia foi confirmada em getters independentes, setter, construtor e RTTI. Como a propriedade de cenário é genericamente `IComponent`, a vtable exata `0x04FA1CE8` é exigida antes de acessar `+0xD60`. O mundo exige a vtable `0x04ECA2E0`. O diagnóstico não depende mais de `camera.World`.

Os getters fornecem o nível padrão em `world + 0x148` e a área raiz em `level + 0xF0`. A coleção `ActiveLevels` usa sentinela em `world + 0x160` e valor de nó em `+0x10`; `SubAreas` usa sentinela em `level + 0x100` e valor em `+0x30`; `IArea.Entities` usa sentinela em `area + 0xB8` e valor em `+0x10`. Iteradores refletidos e construtores corroboram esses layouts. Somente links e ponteiros de valor são lidos; chaves e nomes das coleções não são coletados.

O diagnóstico percorre o nível padrão e os ativos, suas áreas raiz e subáreas imediatas, com deduplicação. Os limites são 16 níveis, 32 áreas e 256 nós de entidades globalmente, incluindo nós repetidos que apontam para o mesmo objeto. Todas as leituras compartilham o limite de 4.096 chamadas e 64 KiB solicitados; a CLI encerra novas leituras após uma janela de 2 segundos. Arestas de propriedade, nós e sentinelas são rechecados. Isso detecta algumas alterações, sem produzir um snapshot atômico.

Para tipos `IEntity` conhecidos com base no deslocamento zero, o helper de `GetEntityPosition` em RVA `0x008977B0` soma a translação float em `+0x7C` à translação inteira assinada em `+0x1AC`. Somente quando os três inteiros são `INT32_MAX` a parcela inteira é zerada. O relatório inclui até 16 amostras com posição e projeção matemática, priorizando objetos com esqueleto sem classificá-los como jogadores. Tipos desconhecidos recebem apenas contagens por RVA de vtable e nenhuma leitura de campos tipados. Endereços absolutos de objetos e nomes de jogadores não são emitidos.

O formato v3 também revalida as referências de propriedade e os cabeçalhos das coleções quando encontra o limite de nós. Nesse caso, `consistency_recheck_passed: true` significa que essas referências permaneceram iguais nas releituras; `collection_complete` continua falso. Nem essa rechecagem nem as leituras de posições produzem um snapshot atômico.

## Campos adicionais de identidade

- `ClientPlayer.PrincipleRole`: o wrapper `0x00D0E7A0` dereferencia o resultado do getter `0x00CFD9E0`, que aponta para `player + 0xDD8`. O ponteiro é usado somente para comparação de identidade com objetos observados e é rechecado no final. Sua ausência no prefixo coletado é informada, sem concluir que não existe personagem local.
- `IEntity.CharCtrl`: o binding leva ao getter `0x0095BF30`, que lê o byte em `+0xA7`. O bit alto indica um índice de componente; quando presente, o ponteiro é obtido de `*(entity + 0x40) + (tag & 0x7F) * 8`. Só a presença é registrada, sem seguir esse componente ou deduzir equipe/vida.
- `SkeletonComponent.ResourcePath`: binding em `0x05366E30`, wrapper `0x011C2B30`, ponteiro de membro em `0x05375750` e getter `0x00CC6F70`, com ajuste de objeto zero e campo em `skeleton + 0x240`. RTTI exige a vtable primária `0x04FA22A0`; a vtable secundária em `0x04FA2390` não autoriza essa leitura.
- O adaptador de string em `0x0058B2A0` confirma comprimento/capacidade em `+0x10`/`+0x18`, armazenamento inline de até 15 caracteres e ponteiro para strings maiores. A leitura é limitada a 192 bytes, com rechecagem do cabeçalho. Só identificadores relativos ASCII são emitidos; caminhos absolutos, caracteres de controle e segmentos `..` são omitidos. Não são abertos arquivos nem lidos nomes de jogadores.

O relatório agrupa até 32 identificadores de recurso e prioriza, nas amostras de posição, esqueletos não locais com controlador de movimento. Essa prioridade é uma assinatura técnica, não uma classificação de jogador ou inimigo. Armas, acessórios e objetos animados também podem ter esqueleto. Os novos campos ainda precisam de validação em execução.

O relatório v3 recebido encontrou a referência local, 7 objetos com controlador de movimento e 6 amostras não locais com esqueleto e controlador. A rechecagem de propriedade do prefixo passou, mas a coleta continuou incompleta no limite de 256 nós. Dos 27 esqueletos observados nesse recorte, 13 usavam `SkeletonComponent` e 14 usavam o RVA `0x0559B8F8`, então desconhecido pelo leitor de recursos. Nenhum identificador de recurso foi emitido.

O RVA `0x0559B8F8` foi resolvido estaticamente: o COL `0x06F72620` identifica `ActorComponent`, com objeto completo e base `SkeletonComponent` no deslocamento zero. A versão v4 permite o campo herdado `ResourcePath` para esse tipo. Se o campo estiver vazio, lê separadamente `ActorComponent.GraphPath`: descritor `0x05662B30`, wrapper `0x01E292D0`, ponteiro de membro `0x05684260`, ajuste zero, getter `0x00CE8150`, campo `+0x3A0`. O tipo refletido usa o mesmo adaptador de string. O relatório informa `resource_source` como `model_resource` ou `animation_graph`; um grafo não é apresentado como recurso de modelo. Tipos secundários e desconhecidos continuam bloqueados para campos tipados.

O relatório v4 recebido passou na leitura e rechecagem do prefixo. As seis amostras não locais com controlador também tinham `ActorComponent`, mas os campos de modelo e grafo ficaram vazios. Portanto, esses campos não forneceram uma identificação adicional nessa execução. Vida e equipe continuam desconhecidas.

## Conferência visual de projeção

`zyon-calibration.exe` desenha marcadores de origem sobre a área cliente do jogo em modo janela/sem bordas, sem copiar seus pixels. Usa a mesma verificação de identidade e os mesmos limites de leitura dos diagnósticos; tenta atualizar a coleta a cada 16 ms somente quando o jogo está em primeiro plano. Renova a câmera ao final da coleta, dentro do mesmo orçamento de leituras, e solicita pintura imediata. Amostras que levam 50 ms ou mais são descartadas; a frequência efetiva depende do Windows e do custo das leituras. As observações continuam sem atomicidade e sem previsão de movimento. Encerra após 120 segundos, com End ou ao terminar o processo do jogo. A janela não recebe cliques ou foco e não envia entrada ao jogo.

Só exibe candidatos nativos conhecidos com `ActorComponent`, esqueleto e controlador, diferentes da referência local, em uma amostra cuja rechecagem de propriedade passou. Pontos fora do viewport ou dos planos near/far são descartados antes da conversão para pixels inteiros; isso evita rasterizar as coordenadas enormes de pontos quase sobre o plano da câmera. Divergência entre viewport e janela suspende os marcadores. O limite de desenho é 64 pontos; a coleta permanece parcial quando encontra o limite de 256 nós.

O filtro é técnico e não estabelece jogadores/inimigos. Os marcadores não são caixas, ossos, barras de vida ou pontos de mira. A seleção/projeção foi testada com dados analíticos, e o desenho/callbacks de transparência foram exercitados com GDI no Wine. A contagem de recursos GDI não está disponível nesse Wine. O usuário confirmou o alinhamento visual depois de reduzir o intervalo para 16 ms e renovar a câmera ao final da coleta. Relatou também que o ponto permanece após a morte do boneco; isso não confirma um estado de vida.

Testes reproduzem `camera.World` nulo, níveis/áreas sem raiz, coleções sobrepostas, ciclos, mudanças durante a leitura, falhas de tipo, posições inválidas, limites globais, rechecagem de prefixo, referência local alterada e leitura limitada de recursos. As amostras privadas de posição e os relatórios do usuário não são publicados neste repositório.

## Validação que falta

- Confirmar o caminho de cena em execução e sua cobertura dos objetos relevantes.
- Identificar a coleção de jogadores e separar entidades de cenário, aliados e inimigos.
- Identificar vida atual/máxima e estados de jogador. As ocorrências de health examinadas pertencem a objetos destrutíveis e não estabelecem a vida dos jogadores.
- Validar posições/ossos e correspondência entre pixels e orientação da câmera.

A comparação de fases abaixo é a próxima verificação no Windows. O painel continua sendo uma prévia com alvos sintéticos; ESP e aimbot no jogo ainda não foram demonstrados.

## Comparação exploratória de fases

`zyon-state-compare.exe` usa F6/F7/F8 para rótulos fornecidos pelo usuário, sem validar vida, morte ou reaparecimento. F6 seleciona um candidato próximo do centro da câmera (até 240 pixels e margem mínima de 16 pixels para o segundo candidato); F7 procura a mesma referência na nova coleção. F8 seleciona novamente o candidato indicado pelo usuário. Não deduz continuidade se o endereço mudar. Mesmo um endereço igual pode ter sido reutilizado; não é prova de identidade ou tempo de vida da alocação. Uma coleta parcial sem a referência é informada como não observada, sem declarar destruição ou morte.

A coleta exige os mesmos guards e limites globais da cena: 4.096 leituras e 64 KiB solicitados, com janela de início de leituras de dois segundos por fase. Somente o IEntity genérico de vtable RVA `0x04F2A180` pode fornecer o prefixo. Lê 440 bytes (`0x1B8`), terminando nos campos de tradução inteira já verificados, com vtable conferida antes, dentro e depois do bloco. Não segue ponteiros dos bytes capturados, nem percorre componentes ou objetos Python adicionais. As observações continuam sem atomicidade.

O JSON contém no máximo 64 diferenças de palavras de 32 bits entre as três fases, somente quando todas foram capturadas no mesmo endereço nativo. Mostra interpretações u32/f32; floats não finitos aparecem como null. Omite os campos de tradução conhecidos e qualquer slot alinhado de oito bytes que tenha aparência de ponteiro em alguma fase; essa filtragem conservadora também pode omitir escalares. Não serializa o prefixo bruto, ponteiros remotos ou strings de memória. Mudanças de animação, estado nativo, temporizadores e padding não estabelecem um campo de vida. A vida pode estar fora desse prefixo, inclusive nos scripts ou componentes; um relatório sem diferenças úteis é possível.

Testes sintéticos cobrem seleção ambígua, exclusão local, cena instável, alvo não observado em coleta parcial, mudança de tipo durante leitura, limites compartilhados, omissão de ponteiros e comparação impedida entre referências diferentes. O teste Windows também confere JSON e rejeição de jogo ausente ou build diferente. A captura real das três fases ainda depende do teste do usuário.
