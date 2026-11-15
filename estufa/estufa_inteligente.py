from flask import Flask, render_template, request, redirect, url_for
from models.plantas import PLANTAS_DISPONIVEIS
from models.otimizacao import resolver_modelo_po
import traceback

app = Flask(__name__)

@app.route('/')
def index():
    try:
        return render_template('index.html', plantas=PLANTAS_DISPONIVEIS)
    except Exception as e:
        return f"<h3>Erro na página inicial:</h3><pre>{traceback.format_exc()}</pre>"

@app.route('/simulacao', methods=['GET', 'POST'])
def simulacao():
    try:
        if request.method == 'POST':
            planta_nome = request.form.get('planta', 'Girassol')
            peso_custo = float(request.form.get('peso_custo', 50))
            peso_tempo = float(request.form.get('peso_tempo', 50))
        else:
            planta_nome = 'Girassol'
            peso_custo = 50.0
            peso_tempo = 50.0

        if planta_nome not in PLANTAS_DISPONIVEIS:
            planta_nome = 'Girassol'

        planta = PLANTAS_DISPONIVEIS[planta_nome]
        resultado = resolver_modelo_po(planta, peso_custo, peso_tempo)
        
        return render_template('simulacao.html', planta=planta, resultado=resultado, peso_custo=peso_custo, peso_tempo=peso_tempo)
    
    except Exception as e:
        # Mostra o erro detalhado diretamente no ecrã para sabermos o que falhou
        return f"<div style='padding:20px; font-family:sans-serif;'><h2 style='color:red;'>⚠️ Erro Detalhado no Servidor Flask:</h2><pre>{traceback.format_exc()}</pre><br><a href='/' style='padding:10px 15px; background:#2e7d32; color:white; text-decoration:none; border-radius:5px;'>Voltar ao Início</a></div>", 500

@app.route('/relatorio', methods=['POST'])
def relatorio():
    try:
        planta_nome = request.form.get('planta', 'Girassol')
        peso_custo = float(request.form.get('peso_custo', 50))
        peso_tempo = float(request.form.get('peso_tempo', 50))

        planta = PLANTAS_DISPONIVEIS.get(planta_nome, PLANTAS_DISPONIVEIS["Girassol"])
        
        resultado = {
            "agua": float(request.form.get('agua', 0)),
            "potencia_luz_w": int(request.form.get('potencia_luz_w', 0)),
            "luz_minutos": float(request.form.get('luz_minutos', 0)),
            "luz_horas_equivalente": float(request.form.get('luz_horas_equivalente', 0)),
            "consumo_wh": float(request.form.get('consumo_wh', 0)),
            "consumo_kwh": float(request.form.get('consumo_kwh', 0)),
            "nutrientes": float(request.form.get('nutrientes', 0)),
            "tempo_otimizado": int(request.form.get('tempo_otimizado', 10)),
            "z_total": float(request.form.get('z_total', 0)),
            "peso_custo": peso_custo,
            "peso_tempo": peso_tempo,
            "custos_individuais": {
                "agua": float(request.form.get('custo_agua', 0)),
                "eletrica": float(request.form.get('custo_eletrica', 0)),
                "nutrientes": float(request.form.get('custo_nutrientes', 0)),
                "tempo": float(request.form.get('custo_tempo', 0))
            }
        }

        return render_template('relatorio.html', planta=planta, resultado=resultado)
    except Exception as e:
        return f"<div style='padding:20px; font-family:sans-serif;'><h2 style='color:red;'>⚠️ Erro ao gerar relatório:</h2><pre>{traceback.format_exc()}</pre></div>", 500

if __name__ == '__main__':
    app.run(debug=True)
