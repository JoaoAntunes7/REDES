from mininet.topo import Topo
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.node import OVSKernelSwitch, DefaultController
from mininet.log import setLogLevel
from time import sleep
import sys
import os
import re
import csv

#Configuração padrão do Mininet:
#O Mininet já aplica automaticamente os seguintes comandos ao usar TCLink(bw=10):
#    tc qdisc add dev <iface> root handle 5: htb default 1
#    tc class add dev <iface> parent 5: classid 5:1 htb rate 10mbit

class RTPTopo(Topo):
    def __init__(self, bw=10):  #valor padrão: 10 Mbit
        self.bw = bw
        super().__init__()
        
    def build(self):
	    s1 = self.addSwitch('s1')
	    s2 = self.addSwitch('s2')

	    h1 = self.addHost('h1') #vídeo origem
	    h2 = self.addHost('h2') #vídeo destino
	    h3 = self.addHost('h3') #iperf origem
	    h4 = self.addHost('h4') #iperf destino

	    self.addLink(h1, s1, cls=TCLink, bw=10) #h1 linkado com s1 com banda larga 10 como padrão
	    self.addLink(h3, s1, cls=TCLink, bw=10)
	    self.addLink(h2, s2, cls=TCLink, bw=10)
	    self.addLink(h4, s2, cls=TCLink, bw=10)
	    self.addLink(s1, s2, cls=TCLink, bw=10)

def show_tc_config(switch, iface):
    print(f"Configuração atual do tc em {iface} de {switch.name}:\n")
    print(switch.cmd(f'tc qdisc show dev {iface}'))
    print(switch.cmd(f'tc class show dev {iface}'))
    print(switch.cmd(f'tc filter show dev {iface}'))

def apply_prio(switch, iface):
    print(f"[QoS] Aplicando escalonamento 'prio' como filha de HTB em {iface} de {switch.name}...")
    
    #Aplica escaloamento prio com 3 filas de prioridade (0 mais prioritária)
    switch.cmd(f'tc qdisc add dev {iface} parent 5:1 handle 50: prio bands 3') 
    
    #Direciona as portas 5004 e 5006, usadas pelo RTP, para a fila 0
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 50: prio 1 u32 match ip dport 5004 0xffff flowid 50:1')
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 50: prio 1 u32 match ip dport 5006 0xffff flowid 50:1')
    
    #Direciona a porta 5001, usada pelo iperf, para a fila 1
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 50: prio 2 u32 match ip dport 5001 0xffff flowid 50:2')
    
    #O outros tráfegos irão automaticamente para a a fila 2
    
def apply_ponderado(switch, iface, bw):
    print(f"[QoS] Aplicando escalonamento 'ponderado' como filha de HTB em {iface} de {switch.name}...")
    
    low_rate = bw//3
    high_rate = bw - low_rate
    
    #Criação de subclasses (irão possuir diferentes larguras de banda)
    switch.cmd(f'tc class add dev {iface} parent 5:1 classid 5:10 htb rate {low_rate}mbit ceil {low_rate}mbit')
    switch.cmd(f'tc class add dev {iface} parent 5:1 classid 5:20 htb rate {high_rate}mbit ceil {high_rate}mbit')
    
    #Aplica fila padrão para cada fluxo
    switch.cmd(f'tc qdisc add dev {iface} parent 5:10 handle 10: pfifo')
    switch.cmd(f'tc qdisc add dev {iface} parent 5:20 handle 20: pfifo')
    
    #Direciona as portas 5004 e 5006, usadas pelo RTP, para a classe de maior prioridade
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 5: prio 1 u32 match ip dport 5004 0xffff flowid 5:20')
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 5: prio 1 u32 match ip dport 5006 0xffff flowid 5:20')
    
    #Direciona a porta 5001, usada pelo iperf, para a classe de menor prioridade
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 5: prio 2 u32 match ip dport 5001 0xffff flowid 5:10')
    
    
#apply_tbf deve ser aplicado diretamento no host3 (que está usando iperf), se não o método TBF impactará todo o tráfego pelos switches -> solução: criação de subclasses

def apply_tbf(switch, iface, bw, burst='32kbit', latency='400ms'): #possui burst e latency padrão para balde furado
    if burst != '32kbit':
        print(f"[QoS] Aplicando 'Balde Furado com Tokens' como filha de HTB em {iface} de {switch.name}...")
    else:
        print(f"[QoS] Aplicando 'Balde Furado' como filha de HTB em {iface} de {switch.name}...")
        
    low_rate = bw//3
    high_rate = bw - low_rate
    
    #Criação de subclasses (irão possuir diferentes larguras de banda)
    switch.cmd(f'tc class add dev {iface} parent 5:1 classid 5:10 htb rate {low_rate}mbit ceil {low_rate}mbit')
    switch.cmd(f'tc class add dev {iface} parent 5:1 classid 5:20 htb rate {high_rate }mbit ceil {high_rate }mbit')
    
    #Aplica TBF por fluxo
    switch.cmd(f'tc qdisc add dev {iface} parent 5:10 handle 10: tbf rate {low_rate}mbit burst {burst} latency {latency}')
    switch.cmd(f'tc qdisc add dev {iface} parent 5:20 handle 20: tbf rate {high_rate}mbit burst {burst} latency {latency}')
    
    #Filtra por porta e direciona para a classe correta
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 5: prio 1 u32 match ip dport 5001 0xffff flowid 5:10')
    
#Reserva de recursos vai garantir que o tráfego do RTP tenha uma banda mínima mesmo em situações de alto congestionamento
def apply_reserva(switch, iface, bw):
    print(f"[QoS] Aplicando 'Reserva de Recursos' como filha de HTB em {iface} de {switch.name}...")
    
    low_rate = bw//2.5
    high_rate = bw - low_rate
    
    #Criação de subclasses (irão possuir diferentes larguras de banda)
    switch.cmd(f'tc class add dev {iface} parent 5:1 classid 5:30 htb rate {high_rate}mbit ceil {bw}mbit')
    switch.cmd(f'tc class add dev {iface} parent 5:1 classid 5:40 htb rate {low_rate}mbit ceil {bw}mbit')
    
    #Aplica fila para cada fluxo
    switch.cmd(f'tc qdisc add dev {iface} parent 5:30 handle 30: pfifo')
    switch.cmd(f'tc qdisc add dev {iface} parent 5:40 handle 40: pfifo')
    
     #Direciona as portas 5004 e 5006, usadas pelo RTP, para a classe de maior prioridade
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 5: prio 1 u32 match ip dport 5004 0xffff flowid 5:30')
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 5: prio 1 u32 match ip dport 5006 0xffff flowid 5:30')
    
    #Direciona a porta 5001, usada pelo iperf, para a classe de menor prioridade
    switch.cmd(f'tc filter add dev {iface} protocol ip parent 5: prio 2 u32 match ip dport 5001 0xffff flowid 5:40')

def run(escalonamento='nenhum', trafego='nenhum', reserva='não'):
    print("Limpando rede anterior...")
    sleep(2)
    os.system('mn -c') 
    os.system('killall ovs-testcontroller') 
    os.system('clear')
    
    print("╔═══════════════════════════════════════════╗")
    print("║            Largura de banda:              ║")
    print("║             Mínimo: 2Mbits                ║")
    print("║             Padrão: 10Mbits               ║")
    print("║             Máximo: 50Mbits               ║")
    print("╠═══════════════════════════════════════════╣")
    print("║          Instâncias paralelas:            ║")
    print("║    Aumentam o congestionamento na rede.   ║")
    print("║  Cada instância possui um tráfego UDP de  ║")
    print("║  100Mbits e TCP de 50Mbits com duração de ║")
    print("║  20 segundos.                             ║")
    print("╚═══════════════════════════════════════════╝")
    bw = int(input("Escolha a largura de banda da rede: "))
    bw = max(2, min(bw, 50))
    num_streams = int(input("Escolha o número de instâncias paralelas entre h3 e h4: "))
    topo = RTPTopo(bw=bw)
    net = Mininet(topo=topo, link=TCLink, switch=OVSKernelSwitch, controller=DefaultController)
    net.start()

    h1, h2, h3, h4 = net.get('h1', 'h2', 'h3', 'h4')
    s1, s2 = net.get('s1', 's2')
    
    #Sobrescreve a largura de banda padrão da rede
    s1.cmd(f'tc class change dev s1-eth3 parent 5: classid 5:1 htb rate {bw}mbit ceil {bw}mbit')
    
    if escalonamento == 'prio':
        apply_prio(s1, 's1-eth3')
    elif escalonamento == 'ponderado':
        apply_ponderado(s1, 's1-eth3', bw)
    
    if trafego == 'lb':
        apply_tbf(s1, 's1-eth3', bw)
    elif trafego == 'tlb':
        apply_tbf(s1, 's1-eth3', bw, burst='256kbit', latency='50ms')
    
    if reserva == 'sim':
        apply_reserva(s1, 's1-eth3', bw)
        
    show_tc_config(s1, 's1-eth3')

    print("Iniciando transmissão RTP de h1 para h2...")

    #Envio do vídeo via h1
    h1.cmd(
        'ffmpeg -re -i video.mp4 '
        '-map 0:v:0 -c:v libx264 -preset ultrafast -tune zerolatency '
        '-x264-params "keyint=25:scenecut=0:repeat-headers=1" '
        '-f rtp rtp://10.0.0.2:5004?pkt_size=1500 '
        '-map 0:a:0 -c:a aac -ar 44100 -b:a 128k '
        '-f rtp rtp://10.0.0.2:5006?pkt_size=1500 ' 
        '-sdp_file video.sdp > /tmp/ffmpeg.log 2>&1 &'
    ) #ps = 1200

    sleep(2)

    print("Iniciando ffplay em h2...")

    #Recebimento do vídeo via h2
    h2.cmd('ffplay -report -protocol_whitelist "file,udp,rtp" -fflags nobuffer -flags low_delay -i video.sdp ''> /tmp/ffplay.log 2>&1 &')

    sleep(2)

    #Monitoramento entre switches
    print("Iniciando monitoramento da interface do link s1 <-> s2..")
    monitor = s1.popen('ifstat -i s1-eth3 0.5', stdout=sys.stdout)

    sleep(10)
    
	#Tráfego entre h3 e h4 -> atrasa a transmissão
    duration = 20
    print(f"Iniciando {num_streams} fluxo(s) iperf UDP de h3 para h4 por {duration} segundos...")
    for i in range(num_streams):
        h3.cmd(f'iperf -c 10.0.0.4 -u -b 200M -t {duration} > /tmp/iperf_{i}.log 2>&1 &') #UDP de 200M
        h3.cmd(f'iperf -c 10.0.0.4 -b 100M -t {duration} > /tmp/iperf_{i}.log 2>&1 &') #TCP de 100M
        
    ping_output = h1.cmd('ping -c 10 10.0.0.2') #pinga o recepctor para pegar informações
    match = re.search(r'rtt min/avg/max/mdev = ([\d\.]+)/([\d\.]+)/([\d\.]+)/([\d\.]+) ms', ping_output)

    if match:
        rtt_min = float(match.group(1))
        rtt_avg = float(match.group(2))
        rtt_max = float(match.group(3))
        jitter = float(match.group(4))  # mdev é uma estimativa de jitter
        
    filename = "resultados.csv"
    write_header = not os.path.exists(filename) #verifica se é a primeira vez acessando o arquivo

    tecnica = f"{escalonamento}+{trafego}+{reserva}" #pega o nome das técnicas que estão sendo utilizadas
    
    with open(filename, mode='a', newline='') as file: #abre arquivo e escreve os dados
        writer = csv.writer(file)
        if write_header:
            writer.writerow(["Tecnica", "Latência Media (ms)", "Jitter (ms)", "Banda Larga (Mbits)", "Nº de Instâncias"])
        writer.writerow([tecnica, rtt_avg, jitter, bw, num_streams])
        print("Escrevendo no csv...")
    
    
    print("Executando experimento por mais 40 segundos...")
    sleep(40)

    print("Encerrando monitoramento...")
    monitor.terminate()

    print("Encerrando rede...")
    net.stop()


if __name__ == '__main__':
    filename = "resultados.csv"
    print("╔═══════════════════════════════════════════╗")
    print("║ Limpar o arquivo csv?                     ║")
    print("║  [1] Não                                  ║")
    print("║  [2] Sim                                  ║")
    print("╚═══════════════════════════════════════════╝")
    limpar_input = int(input("Escolha: "))
    if limpar_input == 2:
        if os.path.exists(filename): 
            os.remove(filename) #exclui arquivo csv
    os.system("clear")

    print("╔═══════════════════════════════════════════╗")
    print("║           MENU DE OPÇÕES QoS              ║")
    print("╠═══════════════════════════════════════════╣")
    print("║ Escalonamento:                            ║")
    print("║  [1] Nenhum                               ║")
    print("║  [2] Prioritário                          ║")
    print("║  [3] Ponderado                            ║")
    print("╠═══════════════════════════════════════════╣")
    print("║ Controle de Tráfego:                      ║")
    print("║  [1] Nenhum                               ║")
    print("║  [2] Balde Furado                         ║")
    print("║  [3] Balde Furado com Tokens              ║")
    print("╠═══════════════════════════════════════════╣")
    print("║ Reserva de Recursos:                      ║")
    print("║  [1] Não                                  ║")
    print("║  [2] Sim                                  ║")
    print("╚═══════════════════════════════════════════╝")
    
    escalonamento_option = input("Escolha o tipo de escalonamento: ").strip()
    trafego_option = input("Escolha o controle de tráfego: ").strip()
    reserva_option = input("Reserva de recursos ativa?: ").strip()
    
    escalonamento = {'1': 'nenhum', '2': 'prio', '3': 'ponderado'}.get(escalonamento_option, 'nenhum')
    trafego = {'1': 'nenhum', '2': 'lb', '3': 'tlb'}.get(trafego_option, 'nenhum')
    reserva = {'1': 'não', '2': 'sim'}.get(reserva_option, 'não')
    
    run(escalonamento, trafego, reserva)
