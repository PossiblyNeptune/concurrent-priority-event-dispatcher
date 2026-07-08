import streamlit as st
import pandas as pd
import subprocess
import os
import time

# Set page configurations
st.set_page_config(
    page_title="Concurrent Event Dispatcher Simulator",
    page_icon="⚡",
    layout="wide",
    initial_sidebar_state="expanded"
)

# App Title & Description
st.title("⚡ Concurrent C++ Event Dispatcher Simulator")
st.markdown("""
This dashboard showcases a **Concurrent Priority Event Dispatcher** implemented in C++17. 
The backend is a multi-threaded task runner designed for soft real-time environments, featuring starvation-free queue aging, bounded backpressure, and shared subscription locks.
""")

# Sidebar Controls for Simulation
st.sidebar.header("🔧 Simulation Parameters")

duration_s = st.sidebar.slider("Simulation Duration (seconds)", min_value=1.0, max_value=10.0, value=3.0, step=0.5)
num_threads = st.sidebar.slider("ThreadPool Worker Threads", min_value=1, max_value=16, value=4, step=1)
bus_capacity = st.sidebar.slider("EventBus Queue Capacity", min_value=10, max_value=200, value=50, step=10)

st.sidebar.subheader("📈 Event Production Frequencies")
input_hz = st.sidebar.slider("INPUT Event Rate (HIGH Priority)", min_value=0.0, max_value=200.0, value=60.0, step=5.0)
physics_hz = st.sidebar.slider("PHYSICS Event Rate (MEDIUM Priority)", min_value=0.0, max_value=200.0, value=50.0, step=5.0)
ai_hz = st.sidebar.slider("AI Event Rate (LOW Priority)", min_value=0.0, max_value=50.0, value=10.0, step=2.0)

# Build Target Check / Compilation on Startup
BINARY_PATH = "./event_dispatcher"

@st.cache_resource
def compile_cpp_backend():
    st.info("🛠️ Compiling C++ Event Dispatcher backend...")
    
    # Compilation command using standard g++ compiler
    compile_cmd = [
        "g++", "-std=c++17", "-pthread", "-Iinclude",
        "src/event_bus.cpp", "src/handler_registry.cpp",
        "src/thread_pool.cpp", "src/stats_sampler.cpp",
        "src/main.cpp", "-o", "event_dispatcher"
    ]
    
    try {
        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode == 0:
            st.success("✅ C++ compilation completed successfully!")
            return True
        else:
            st.error(f"❌ C++ Compilation Failed!\nError:\n{result.stderr}")
            return False
    except Exception as e:
        st.error(f"❌ Failed to locate compiler or run build commands: {e}")
        return False

# Trigger compilation
backend_ready = compile_cpp_backend()

if backend_ready:
    # Run Button
    if st.button("🚀 Start Simulation Run", type="primary"):
        # Reset telemetry file if it exists
        if os.path.exists("stats.csv"):
            os.remove("stats.csv")

        # Set up a progress visualizer
        progress_bar = st.progress(0)
        status_text = st.empty()
        
        status_text.text("Initializing C++ ThreadPool and event generators...")
        progress_bar.progress(10)
        
        # Execute C++ process in the background
        cmd = [
            BINARY_PATH,
            str(duration_s),
            str(num_threads),
            str(input_hz),
            str(physics_hz),
            str(ai_hz),
            str(bus_capacity)
        ]
        
        start_time = time.time()
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        
        # Update progress bar while running
        while process.poll() is None:
            elapsed = time.time() - start_time
            percentage = min(int((elapsed / duration_s) * 90) + 10, 95)
            status_text.text(f"Running simulation: {elapsed:.1f}s / {duration_s:.1f}s elapsed...")
            progress_bar.progress(percentage)
            time.sleep(0.2)
            
        progress_bar.progress(100)
        status_text.text("Simulation completed! Analyzing telemetry...")
        
        stdout, stderr = process.communicate()
        
        if process.returncode != 0:
            st.error(f"Error during simulation execution:\n{stderr}")
        else:
            # Display results
            st.success("🎉 Simulation run completed successfully!")
            
            # Show output log metrics
            with st.expander("📄 View C++ Console Output logs"):
                st.code(stdout, language="text")
                
            # Read CSV statistics
            if os.path.exists("stats.csv"):
                try:
                    df = pd.read_csv("stats.csv")
                    
                    # Columns: Time_MS, High_Size, Med_Size, Low_Size, Input_Latency_MS, Physics_Latency_MS, AI_Latency_MS, Render_Latency_MS, Input_Missed, Physics_Missed, AI_Missed, Render_Missed
                    
                    # Create two-column layout for dashboards
                    col1, col2 = st.columns(2)
                    
                    with col1:
                        st.subheader("📦 EventBus Queue Sizes Over Time")
                        st.line_chart(df.set_index("Time_MS")[["High_Size", "Med_Size", "Low_Size"]])
                        st.caption("Displays the load distribution in HIGH, MEDIUM, and LOW priority lanes. Spikes show backpressure bottlenecks.")
                        
                    with col2:
                        st.subheader("⏱️ Average Event Latencies (Milliseconds)")
                        st.line_chart(df.set_index("Time_MS")[["Input_Latency_MS", "Physics_Latency_MS", "AI_Latency_MS"]])
                        st.caption("Tracks wait times inside the queue. Note that INPUT (HIGH priority) maintains microsecond latency profiles.")
                    
                    col3, col4 = st.columns(2)
                    
                    with col3:
                        st.subheader("⚠️ Cumulative Soft Real-Time Deadline Misses")
                        st.line_chart(df.set_index("Time_MS")[["Input_Missed", "Physics_Missed", "AI_Missed"]])
                        st.caption("Failsafe triggers showing missed deadlines. Saturation occurs if consumer throughput ceiling is exceeded.")
                        
                    with col4:
                        st.subheader("📋 Telemetry Raw CSV Data Logs")
                        st.dataframe(df)
                        
                except Exception as ex:
                    st.error(f"Failed to parse stats.csv output: {ex}")
            else:
                st.warning("⚠️ No stats.csv file was generated during this simulation.")
else:
    st.warning("⚠️ C++ dispatcher backend is not compiled. Please verify your workspace contains gcc / build-essential tools.")
