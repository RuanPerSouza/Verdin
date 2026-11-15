class Planta:
    def __init__(self, nome, icone, agua_min, potencia_luz_w, luz_min_minutos, nutrientes_min, tempo_des_min, frequencia_rega, custo_agua_ml, custo_eletrica_kwh, custo_nutriente_g, melhor_epoca, pior_epoca):
        self.nome = nome
        self.icone = icone
        self.agua_min = agua_min                        # ml/dia
        self.potencia_luz_w = potencia_luz_w            # Potência da iluminação em Watts (W)
        self.luz_min_minutos = luz_min_minutos          # Tempo de iluminação em min/dia
        self.nutrientes_min = nutrientes_min            # gramas/dia
        self.tempo_des_min = tempo_des_min              # segundos base da simulação
        self.frequencia_rega = frequencia_rega          # segundos entre regas
        
        # Custos unitários
        self.custo_agua_ml = custo_agua_ml              # R$ por ml
        self.custo_eletrica_kwh = custo_eletrica_kwh      # R$ por kWh
        self.custo_nutriente_g = custo_nutriente_g      # R$ por grama
        
        self.melhor_epoca = melhor_epoca
        self.pior_epoca = pior_epoca

# Banco de dados com as 9 plantas e unidades convertidas para minutos e Watts
PLANTAS_DISPONIVEIS = {
    "Girassol": Planta(
        "Girassol", "🌻", agua_min=300, potencia_luz_w=50, luz_min_minutos=480, nutrientes_min=15, tempo_des_min=20, frequencia_rega=5,
        custo_agua_ml=0.0005, custo_eletrica_kwh=0.75, custo_nutriente_g=0.35,
        melhor_epoca="Primavera / Verão (Dias longos e alta insolação)",
        pior_epoca="Inverno (Insolação reduzida e dias curtos)"
    ),
    "Rosa": Planta(
        "Rosa", "🌹", agua_min=250, potencia_luz_w=40, luz_min_minutos=360, nutrientes_min=12, tempo_des_min=18, frequencia_rega=6,
        custo_agua_ml=0.0005, custo_eletrica_kwh=0.75, custo_nutriente_g=0.35,
        melhor_epoca="Primavera (Temperaturas amenas, 15°C a 25°C)",
        pior_epoca="Verão Rigoroso / Inverno Intenso (Estresse térmico)"
    ),
    "Tulipa": Planta(
        "Tulipa", "🌷", agua_min=200, potencia_luz_w=35, luz_min_minutos=300, nutrientes_min=10, tempo_des_min=15, frequencia_rega=7,
        custo_agua_ml=0.0005, custo_eletrica_kwh=0.75, custo_nutriente_g=0.35,
        melhor_epoca="Inverno / Início da Primavera (Clima frio)",
        pior_epoca="Verão Quente (Altas temperaturas impedem o florescimento)"
    ),
    "Cacto": Planta(
        "Cacto", "🌵", agua_min=50, potencia_luz_w=60, luz_min_minutos=600, nutrientes_min=5, tempo_des_min=12, frequencia_rega=10,
        custo_agua_ml=0.0005, custo_eletrica_kwh=0.75, custo_nutriente_g=0.35,
        melhor_epoca="Verão (Alta captação de luz natural)",
        pior_epoca="Inverno Chuvoso (Excesso de umidade e risco de apodrecimento)"
    ),
    "Manjericão": Planta(
        "Manjericão", "🌿", agua_min=220, potencia_luz_w=40, luz_min_minutos=360, nutrientes_min=14, tempo_des_min=15, frequencia_rega=5,
        custo_agua_ml=0.0005, custo_eletrica_kwh=0.75, custo_nutriente_g=0.35,
        melhor_epoca="Verão (Clima quente e úmido)",
        pior_epoca="Inverno (Sensível a geadas e frio abaixo de 10°C)"
    ),
    "Hortelã": Planta(
        "Hortelã", "🍃", agua_min=280, potencia_luz_w=35, luz_min_minutos=300, nutrientes_min=16, tempo_des_min=15, frequencia_rega=4,
        custo_agua_ml=0.0005, custo_eletrica_kwh=0.75, custo_nutriente_g=0.35,
        melhor_epoca="Primavera / Outono (Solo úmido e meia-sombra)",
        pior_epoca="Verão Seco (Desidratação rápida das folhas)"
    ),
    "Tomate": Planta(
        "Tomate", "🍅", agua_min=400, potencia_luz_w=50, luz_min_minutos=480, nutrientes_min=25, tempo_des_min=25, frequencia_rega=4,
        custo_agua_ml=0.0005, custo_eletrica_kwh=0.75, custo_nutriente_g=0.35,
        melhor_epoca="Primavera / Verão (Luz abundante e calor moderado)",
        pior_epoca="Inverno (Falta de luz e desenvolvimento estagnado)"
    ),
    "Alface": Planta(
        "Alface", "🥬", agua_min=350, potencia_luz_w=35, luz_min_minutos=300, nutrientes_min=18, tempo_des_min=15, frequencia_rega=4,
        custo_agua_ml=0.0005, custo_eletrica_kwh=0.75, custo_nutriente_g=0.35,
        melhor_epoca="Outono / Inverno (Temperaturas amenas)",
        pior_epoca="Verão Intenso (Florescimento precoce e amargor nas folhas)"
    ),
    "Morango": Planta(
        "Morango", "🌱", agua_min=300, potencia_luz_w=45, luz_min_minutos=420, nutrientes_min=20, tempo_des_min=20, frequencia_rega=5,
        custo_agua_ml=0.0005, custo_eletrica_kwh=0.75, custo_nutriente_g=0.35,
        melhor_epoca="Outono / Primavera (Clima fresco e boa insolação)",
        pior_epoca="Verão Muito Quente (Frutos menores e queimados pelo sol)"
    )
}