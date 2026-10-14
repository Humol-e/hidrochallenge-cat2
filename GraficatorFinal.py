
#Código creado por Emiliano Castro: https://github.com/Humol-e 
#librerías
import plotly.graph_objects as go
import streamlit as st
import pandas as pd
import serial
import serial.tools.list_ports
import time
import plotly.express as px
from skimage import io
import toml
from toml.decoder import TomlDecodeError

st.set_page_config(layout='wide', initial_sidebar_state='expanded')
st.title("Visualización de datos.")
st.sidebar.header('Zona analítica `v1.0`') #C:\Users\Pc\Documents\CODES\hidrochallenge-cat2\GraficatorFinal.py
st.sidebar.image("C:/Users/Pc/Documents/CODES/hidrochallenge-cat2/deltaleon.png", width="stretch")
colu1, colu2 = st.columns(2)
tageo = st.empty()
def get_available_ports():
    ports = list(serial.tools.list_ports.comports())
    return [port.device for port in ports]

available_ports = get_available_ports()
with colu1:
    with st.popover("Selecciona el puerto COM"):
        if st.button("Actualizar puertos"):
            st.rerun()
        st.write("Puertos disponibles :", available_ports)
        selected_port = st.selectbox(
            "Selecciona el puerto COM",
            available_ports,
            index=0 if available_ports else None,
            placeholder="No hay puertos COM detectados"
        )

# Inicializar el DataFrame según el formato:
# millis|etapa|temp|hum|presion|altitud|lat|lon|ax|ay|az|gx|gy|gz|mx|my|mz
valores = [
    "Millis", "Etapa", "Temperatura", "Humedad", "Presion", "Altitud",
    "Latitud", "Longitud", "AceleracionX", "AceleracionY", "AceleracionZ",
    "GiroscopioX", "GiroscopioY", "GiroscopioZ", "MagnetometroX",
    "MagnetometroY", "MagnetometroZ"
]

with colu2:
    with st.popover("Datos a procesar"):
        num_valores =len(valores)

        tageo = st.multiselect("Selecciona que datos vas a procesar.", valores, max_selections= num_valores)
df = pd.DataFrame(columns=valores + ["VelocidadX", "VelocidadY"])

start_button = st.button("Iniciar Lectura", disabled=not selected_port)

dt = .25 #diferencia de tiempo entre cada lectura, usasdo para calcular la velocidad
velocidad = 0.0
velocidadx = 0.0
altitud_maxima = 0.0

metricas = st.columns(6)
tiempo_placeholder = metricas[0].empty()
temperatura_placeholder = metricas[1].empty()
humedad_placeholder = metricas[2].empty()
altitud_placeholder = metricas[3].empty()
altitud_maxima_placeholder = metricas[4].empty()
etapa_placeholder = metricas[5].empty()
# Función para procesar una línea de datos del puerto serial

def procesar_linea_serial(linea):
    try:
        global velocidad, velocidadx
        parts = [parte.strip() for parte in linea.split('|')]
        if len(parts) == num_valores:
            datos = [float(parte) for parte in parts]
            velocidad += datos[9] * dt
            velocidadx += datos[8] * dt
            return {i: datos[i] for i in range(num_valores)}
        else:
            st.error(
                f"Se esperaban {num_valores} datos y se recibieron "
                f"{len(parts)}: {linea}"
            )
            return None
    except Exception as e:
        st.error(f"Error al procesar la línea de datos: {e}")
        return None
def convertir_claves(data, valores):
    resultado = {valores[i]: data[i] for i in range(len(valores))}
    resultado["VelocidadX"] = velocidadx
    resultado["VelocidadY"] = velocidad
    return resultado


# Función para leer datos en lotes
def leer_lote_serial(ser, lote_size=10):
    lineas = []
    for _ in range(lote_size):
        linea = ser.readline().decode('utf-8', errors='ignore').strip()
        if linea:
            lineas.append(linea)
    return lineas
miau = 1

colv1,colv2 = st.columns(2)
velocii = colv1.empty()
velocigraf = colv2.empty()
colx1,colx2 = st.columns(2)
velociix = colx1.empty()
velocigrafx = colx2.empty()
tablatodo_placeholder = st.empty()
with st.sidebar:
    selected_charts = st.multiselect(
        "Selecciona las gráficas a mostrar:",
        [
            "Presión y altitud", "Temperatura y altitud", "Aceleración Y",
            "Etapa y altitud", "Ubicación GPS", "Velocidades", "Tabla de datos"
        ],
        default=["Presión y altitud", "Temperatura y altitud", "Velocidades"]
    )

chart_presion_altitud = st.empty()
chart_temperatura_altitud = st.empty()
chart_aceleracion_y = st.empty()
chart_etapa_altitud = st.empty()
chart_gps = st.empty()
chart_velocidades = st.empty()


def mostrar_gauges(df):
    veloz = df["VelocidadY"].iloc[-1]

    figura = go.Figure(go.Indicator(
        mode="gauge+number", value=veloz, domain={'x': [0, 1], 'y': [0, 1]},
        title={'text': "Velocidad vertical"},
        gauge={'axis': {'range': [-100, 500]}, 'steps': [
            {'range': [-100, 100], 'color': '#e1f226'},
            {'range': [101, 300], 'color': '#ff1818'},
            {'range': [300, 450], 'color': '#660000'}
        ], 'threshold': {'line': {'color': 'red', 'width': 4},
                         'thickness': 0.75, 'value': 490}}
    ))
    velocii.plotly_chart(figura, use_container_width=True)

    veloz_x = df["VelocidadX"].iloc[-1]
    figura = go.Figure(go.Indicator(
        mode="gauge+number", value=veloz_x, domain={'x': [0, 1], 'y': [0, 1]},
        title={'text': "Velocidad horizontal"},
        gauge={'axis': {'range': [-100, 500]}, 'steps': [
            {'range': [-100, 100], 'color': '#e1f226'},
            {'range': [101, 300], 'color': '#ff1818'},
            {'range': [300, 450], 'color': '#660000'}
        ], 'threshold': {'line': {'color': 'red', 'width': 4},
                         'thickness': 0.75, 'value': 490}}
    ))
    velociix.plotly_chart(figura, use_container_width=True)

def actualizar_metricas(df):
    global altitud_maxima
    ultimo = df.iloc[-1]
    altitud_maxima = max(altitud_maxima, ultimo["Altitud"])
    tiempo_placeholder.metric("Tiempo (ms)", f"{ultimo['Millis']:.0f}")
    temperatura_placeholder.metric("Temperatura (C)", f"{ultimo['Temperatura']:.2f}")
    humedad_placeholder.metric("Humedad (%)", f"{ultimo['Humedad']:.2f}")
    altitud_placeholder.metric("Altitud", f"{ultimo['Altitud']:.2f}")
    altitud_maxima_placeholder.metric("Altitud máxima", f"{altitud_maxima:.2f}")
    etapa_placeholder.metric("Etapa del cohete", f"{ultimo['Etapa']:.0f}")


def grafica_dos_ejes(df, x, y1, y2, titulo, etiqueta_y1, etiqueta_y2):
    figura = go.Figure()
    figura.add_trace(go.Scatter(x=df[x], y=df[y1], name=etiqueta_y1))
    figura.add_trace(go.Scatter(x=df[x], y=df[y2], name=etiqueta_y2, yaxis="y2"))
    figura.update_layout(
        title=titulo,
        xaxis_title=x,
        yaxis=dict(title=etiqueta_y1),
        yaxis2=dict(title=etiqueta_y2, overlaying="y", side="right"),
        legend=dict(orientation="h")
    )
    return figura


def mostrar_graficas(df, selected_charts):
    if "Presión y altitud" in selected_charts:
        chart_presion_altitud.plotly_chart(
            grafica_dos_ejes(
                df, "Millis", "Presion", "Altitud",
                "Presión y altitud", "Presión", "Altitud"
            ),
            use_container_width=True
        )
    else:
        chart_presion_altitud.empty()

    if "Temperatura y altitud" in selected_charts:
        chart_temperatura_altitud.plotly_chart(
            grafica_dos_ejes(
                df, "Millis", "Temperatura", "Altitud",
                "Temperatura y altitud", "Temperatura", "Altitud"
            ),
            use_container_width=True
        )
    else:
        chart_temperatura_altitud.empty()

    if "Aceleración Y" in selected_charts:
        figura = px.line(
            df, x="Millis", y="AceleracionY",
            title="Aceleración vertical respecto al tiempo"
        )
        chart_aceleracion_y.plotly_chart(figura, use_container_width=True)
    else:
        chart_aceleracion_y.empty()

    if "Etapa y altitud" in selected_charts:
        chart_etapa_altitud.plotly_chart(
            grafica_dos_ejes(
                df, "Millis", "Etapa", "Altitud",
                "Etapa y altitud", "Etapa", "Altitud"
            ),
            use_container_width=True
        )
    else:
        chart_etapa_altitud.empty()
    if "Velocidades" in selected_charts:
        velocidades = px.line(
            df, x="Millis", y=["VelocidadX", "VelocidadY"],
            title="Velocidades calculadas"
        )
        chart_velocidades.plotly_chart(velocidades, use_container_width=True)
    else:
        chart_velocidades.empty()
    if "Ubicación GPS" in selected_charts:
        mapa = df[["Latitud", "Longitud"]].rename(
            columns={"Latitud": "lat", "Longitud": "lon"}
        )
        chart_gps.map(mapa, latitude="lat", longitude="lon", size=20)
    else:
        chart_gps.empty()

def mostrar_tabla(df):

    tablatodo_placeholder.dataframe(df)



if  start_button:
    try:
        ser = serial.Serial(selected_port, 115200, timeout=1)

        while True:
            # Leer lote de líneas
            lineas = leer_lote_serial(ser, lote_size=5)
            for linea in lineas:
                data = procesar_linea_serial(linea)

                if data:
                    data = convertir_claves(data, valores)  # Convertir claves numéricas a cadenas
                    new_row = pd.DataFrame(data, index=[0])
                    df = pd.concat([df, new_row], ignore_index=True)
                else :
                    st.error(f"Datos incompletos recibidos del puerto serial: {linea}")
            # Actualizar vista
            if not df.empty:
                actualizar_metricas(df)
                mostrar_tabla(df)
                mostrar_gauges(df)
                mostrar_graficas(df, selected_charts)
            time.sleep(0.1)

    except serial.SerialException as e:
        st.error(f"Error al abrir el puerto {selected_port}: {e}")
    except PermissionError as e:
        st.error(f"Permiso denegado para abrir el puerto {selected_port}: {e}")
    except Exception as e:          
        st.error(f"Error inesperado: {e}")

else:
    st.warning("Por favor, selecciona un puerto COM disponible.")



