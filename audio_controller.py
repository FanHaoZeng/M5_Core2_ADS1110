import sounddevice as sd
import numpy as np
import wave
import time
from datetime import datetime
import threading
import os
import csv
import queue
import keyboard
import matplotlib.pyplot as plt
from matplotlib.backends.backend_agg import FigureCanvasAgg
import tkinter as tk
from tkinter import ttk, messagebox
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import sys
import tempfile

class AudioRecorder:
    def __init__(self):
        # 基本设置
        self.sample_rate = 44100
        self.channels = 1
        self.recording = False
        self.paused = False
        self.audio_queue = queue.Queue()
        self.recorded_data = []
        self.start_time = None
        self.pause_time = 0
        
        # 音频流相关
        self.stream = None
        self.preview_stream = None
        self.generator_stream = None
        
        # 线程相关
        self.generator_thread = None
        self.preview_thread = None
        self.update_plot_timer = None
        
        # 状态标志
        self.generating = False
        self.previewing = False
        self.is_closing = False
        
        # 声音生成相关
        self.generator_start_time = None
        self.preview_start_time = None
        
        # 创建必要的目录
        self.recordings_dir = "recordings"
        self.logs_dir = "logs"
        self.use_temp_dir = False
        
        # 尝试创建目录，如果失败则使用临时目录
        try:
            for directory in [self.recordings_dir, self.logs_dir]:
                if not os.path.exists(directory):
                    os.makedirs(directory)
        except (PermissionError, OSError) as e:
            print(f"无法创建目录: {e}")
            # 使用临时目录
            temp_dir = tempfile.gettempdir()
            self.recordings_dir = os.path.join(temp_dir, "audio_recordings")
            self.logs_dir = os.path.join(temp_dir, "audio_logs")
            self.use_temp_dir = True
            
            # 创建临时目录
            for directory in [self.recordings_dir, self.logs_dir]:
                if not os.path.exists(directory):
                    os.makedirs(directory)
        
        # 初始化日志文件
        self.recording_log_file = os.path.join(self.logs_dir, f"recording_log_{datetime.now().strftime('%Y%m%d')}.csv")
        self.sound_log_file = os.path.join(self.logs_dir, f"sound_log_{datetime.now().strftime('%Y%m%d')}.csv")
        
        # 初始化内存日志
        self.recording_logs = []
        self.sound_logs = []
        
        # 检查日志文件是否可写
        self.recording_log_writable = self.check_file_writable(self.recording_log_file)
        self.sound_log_writable = self.check_file_writable(self.sound_log_file)
        
        # 如果日志文件可写，则初始化它们
        if self.recording_log_writable:
            self.initialize_recording_log()
        if self.sound_log_writable:
            self.initialize_sound_log()
        
        # 创建主窗口
        self.root = tk.Tk()
        self.root.title("Audio Tool")
        self.root.geometry("1200x800")  # 设置窗口大小
        
        # 设置窗口关闭行为
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
        
        # 设置默认字体大小
        default_font = ('Arial', 14)
        self.root.option_add('*Font', default_font)
        
        # 创建主框架
        self.main_frame = ttk.Frame(self.root)
        self.main_frame.pack(expand=True, fill='both', padx=10, pady=10)
        
        # 创建左右分栏
        self.left_frame = ttk.Frame(self.main_frame)
        self.left_frame.pack(side='left', expand=True, fill='both', padx=(0, 10))
        
        self.right_frame = ttk.Frame(self.main_frame)
        self.right_frame.pack(side='right', fill='y', padx=(10, 0))
        
        # 初始化界面
        self.init_waveform_display()
        self.init_control_panel()
        
        # 如果使用了临时目录，显示提示
        if self.use_temp_dir:
            messagebox.showinfo("目录信息", 
                               f"由于权限问题，程序将使用临时目录存储文件：\n"
                               f"录音文件: {self.recordings_dir}\n"
                               f"日志文件: {self.logs_dir}")
    
    def check_file_writable(self, filepath):
        """检查文件是否可写"""
        # 如果文件不存在，检查目录是否可写
        if not os.path.exists(filepath):
            directory = os.path.dirname(filepath)
            return os.access(directory, os.W_OK)
        
        # 如果文件存在，检查文件是否可写
        return os.access(filepath, os.W_OK)
    
    def initialize_recording_log(self):
        """初始化录音日志文件"""
        try:
            if not os.path.exists(self.recording_log_file):
                with open(self.recording_log_file, 'w', newline='') as f:
                    writer = csv.writer(f)
                    writer.writerow([
                        'Start Time',
                        'End Time',
                        'Filename',
                        'Duration (s)',
                        'Sample Rate',
                        'Status'
                    ])
        except (PermissionError, OSError) as e:
            print(f"无法创建录音日志文件: {e}")
            self.recording_log_writable = False
    
    def initialize_sound_log(self):
        """初始化声音日志文件"""
        try:
            if not os.path.exists(self.sound_log_file):
                with open(self.sound_log_file, 'w', newline='') as f:
                    writer = csv.writer(f)
                    writer.writerow([
                        'Start Time',
                        'End Time',
                        'Type',
                        'Frequency (Hz)',
                        'Waveform',
                        'Duration (s)',
                        'Volume',
                        'Status'
                    ])
        except (PermissionError, OSError) as e:
            print(f"无法创建声音日志文件: {e}")
            self.sound_log_writable = False
    
    def initialize_log_files(self):
        """Initialize CSV log files - 保留此方法以兼容旧代码"""
        self.initialize_recording_log()
        self.initialize_sound_log()

    def get_timestamp(self):
        """Get formatted timestamp"""
        now = datetime.now()
        return now.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"

    def log_recording(self, filename, duration, status="Success"):
        """Log recording information to CSV"""
        try:
            end_time = self.get_timestamp()
            if self.start_time is None:
                self.start_time = self.get_timestamp()
            
            log_entry = [
                self.start_time,
                end_time,
                filename,
                f"{duration:.2f}",
                self.sample_rate,
                status
            ]
            
            # 始终将日志添加到内存中
            self.recording_logs.append(log_entry)
            
            # 如果日志文件可写，则写入文件
            if self.recording_log_writable:
                try:
                    with open(self.recording_log_file, 'a', newline='') as f:
                        writer = csv.writer(f)
                        writer.writerow(log_entry)
                except Exception as e:
                    print(f"写入录音日志文件失败: {e}")
                    self.recording_log_writable = False
        except Exception as e:
            print(f"记录录音信息失败: {e}")
    
    def log_sound(self, sound_type, frequency, waveform, duration, volume, status="Success"):
        """Log sound generation/preview information to CSV"""
        try:
            end_time = self.get_timestamp()
            if self.start_time is None:
                self.start_time = self.get_timestamp()
            
            log_entry = [
                self.start_time,
                end_time,
                sound_type,
                frequency,
                waveform,
                f"{duration:.2f}",
                f"{volume:.2f}",
                status
            ]
            
            # 始终将日志添加到内存中
            self.sound_logs.append(log_entry)
            
            # 如果日志文件可写，则写入文件
            if self.sound_log_writable:
                try:
                    with open(self.sound_log_file, 'a', newline='') as f:
                        writer = csv.writer(f)
                        writer.writerow(log_entry)
                except Exception as e:
                    print(f"写入声音日志文件失败: {e}")
                    self.sound_log_writable = False
        except Exception as e:
            print(f"记录声音信息失败: {e}")
    
    def audio_callback(self, indata, frames, time, status):
        """音频回调函数"""
        if status:
            print(f"Status: {status}")
        if self.recording and not self.paused and not self.is_closing:
            try:
                self.audio_queue.put(indata.copy())
                # 更新实时波形
                self.recorded_data = np.concatenate([self.recorded_data, indata.flatten()])
                if len(self.recorded_data) > 1000:
                    self.recorded_data = self.recorded_data[-1000:]
            except Exception as e:
                print(f"Error in audio callback: {e}")
                self.stop_recording()
    
    def update_plot(self):
        """更新实时波形图"""
        if self.recording and not self.paused and not self.is_closing:
            try:
                self.line.set_data(range(len(self.recorded_data)), self.recorded_data)
                self.ax.relim()
                self.ax.autoscale_view()
                self.canvas.draw()
            except Exception as e:
                print(f"Error updating plot: {e}")
                self.stop_recording()
        
        # 设置下一次更新
        if self.recording and not self.is_closing:
            self.update_plot_timer = self.root.after(50, self.update_plot)
    
    def save_recording(self, data, filename):
        """保存录音文件"""
        try:
            filepath = os.path.join(self.recordings_dir, filename)
            with wave.open(filepath, 'wb') as wf:
                wf.setnchannels(self.channels)
                wf.setsampwidth(2)
                wf.setframerate(self.sample_rate)
                wf.writeframes((data * 32767).astype(np.int16).tobytes())
            print(f"录音已保存: {filepath}")
            return True
        except Exception as e:
            print(f"保存录音失败: {e}")
            # 尝试使用临时目录
            if not self.use_temp_dir:
                try:
                    temp_dir = tempfile.gettempdir()
                    temp_recordings_dir = os.path.join(temp_dir, "audio_recordings")
                    if not os.path.exists(temp_recordings_dir):
                        os.makedirs(temp_recordings_dir)
                    filepath = os.path.join(temp_recordings_dir, filename)
                    with wave.open(filepath, 'wb') as wf:
                        wf.setnchannels(self.channels)
                        wf.setsampwidth(2)
                        wf.setframerate(self.sample_rate)
                        wf.writeframes((data * 32767).astype(np.int16).tobytes())
                    print(f"录音已保存到临时目录: {filepath}")
                    messagebox.showinfo("保存位置", f"由于权限问题，录音已保存到临时目录:\n{filepath}")
                    return True
                except Exception as e2:
                    print(f"保存到临时目录也失败: {e2}")
                    messagebox.showerror("保存失败", f"无法保存录音文件: {e2}")
                    return False
            else:
                messagebox.showerror("保存失败", f"无法保存录音文件: {e}")
                return False
    
    def toggle_recording(self):
        """切换录制状态"""
        if not self.recording:
            self.start_recording()
        else:
            self.stop_recording()
    
    def toggle_pause(self):
        """切换暂停状态"""
        if self.recording:
            if self.paused:
                self.paused = False
                self.pause_button.config(text="Resume Recording")
                self.status_label.config(text="Recording resumed")
            else:
                self.paused = True
                self.pause_button.config(text="Pause Recording")
                self.status_label.config(text="Recording paused")
    
    def start_recording(self):
        """开始录音"""
        try:
            if self.stream is not None:
                self.stream.stop()
                self.stream.close()
            
            self.recording = True
            self.paused = False
            self.recorded_data = []
            self.start_time = self.get_timestamp()
            
            # 开始录音流
            self.stream = sd.InputStream(
                channels=self.channels,
                samplerate=self.sample_rate,
                callback=self.audio_callback
            )
            self.stream.start()
            
            # 开始更新图表
            self.update_plot()
            self.status_label.config(text="Recording started")
            self.record_button.config(text="Stop Recording")
            
        except Exception as e:
            print(f"Failed to start recording: {e}")
            self.recording = False
            self.status_label.config(text="Failed to start recording")
            self.record_button.config(text="Start Recording")
            if self.stream:
                self.stream.stop()
                self.stream.close()
                self.stream = None
    
    def stop_recording(self):
        """停止录音"""
        if self.recording:
            try:
                self.recording = False
                if self.stream:
                    self.stream.stop()
                    self.stream.close()
                    self.stream = None
                
                # 停止更新图表
                if self.update_plot_timer:
                    self.root.after_cancel(self.update_plot_timer)
                    self.update_plot_timer = None
                
                # 收集所有录音数据
                recorded_data = []
                while not self.audio_queue.empty():
                    try:
                        recorded_data.append(self.audio_queue.get_nowait())
                    except queue.Empty:
                        break
                
                if recorded_data:
                    recorded_data = np.concatenate(recorded_data)
                    duration = len(recorded_data) / self.sample_rate
                    
                    # 生成文件名
                    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                    filename = f"ambient_sound_{timestamp}.wav"
                    
                    # 保存录音
                    if self.save_recording(recorded_data, filename):
                        self.log_recording(filename, duration)
                        self.status_label.config(text=f"Recording saved: {duration:.2f}s")
                    else:
                        self.log_recording(filename, duration, "Save failed")
                        self.status_label.config(text="Failed to save recording")
                else:
                    self.status_label.config(text="Recording stopped")
                
                self.record_button.config(text="Start Recording")
                
            except Exception as e:
                print(f"Failed to stop recording: {e}")
                self.log_recording("unknown", 0, f"Error: {str(e)}")
                self.status_label.config(text="Failed to stop recording")
                self.record_button.config(text="Start Recording")
    
    def init_waveform_display(self):
        """初始化波形显示"""
        # 创建波形显示框架
        waveform_frame = ttk.LabelFrame(self.left_frame, text="Real-time Waveform", padding="5")
        waveform_frame.pack(expand=True, fill='both')
        
        # 初始化实时波形显示
        self.fig, self.ax = plt.subplots(figsize=(10, 6))
        self.line, = self.ax.plot([], [], lw=2)
        self.ax.set_title('Real-time Audio Waveform', fontsize=16)
        self.ax.set_xlabel('Samples', fontsize=14)
        self.ax.set_ylabel('Amplitude', fontsize=14)
        self.ax.set_ylim(-1, 1)
        self.ax.set_xlim(0, 1000)
        
        # 将图表嵌入到Tkinter窗口中
        self.canvas = FigureCanvasTkAgg(self.fig, master=waveform_frame)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(expand=True, fill='both')
        
        # 创建状态标签
        self.status_label = tk.Label(waveform_frame, text="Waiting to start recording...", font=('Arial', 14))
        self.status_label.pack(pady=5)
        
    def init_control_panel(self):
        """初始化控制面板"""
        # 创建录音控制框架
        recording_frame = ttk.LabelFrame(self.right_frame, text="Recording Control", padding="10")
        recording_frame.pack(fill="x", pady=(0, 10))
        
        # 录音按钮
        self.record_button = ttk.Button(recording_frame, text="Start Recording", command=self.toggle_recording)
        self.record_button.pack(fill="x", pady=5)
        
        # 暂停按钮
        self.pause_button = ttk.Button(recording_frame, text="Pause Recording", command=self.toggle_pause)
        self.pause_button.pack(fill="x", pady=5)
        
        # 创建声音生成控制框架
        generator_frame = ttk.LabelFrame(self.right_frame, text="Sound Generation", padding="10")
        generator_frame.pack(fill="x", pady=(0, 10))
        
        # 频率控制
        freq_frame = ttk.Frame(generator_frame)
        freq_frame.pack(fill="x", pady=5)
        ttk.Label(freq_frame, text="Frequency (Hz):").pack(side="left")
        self.freq_var = tk.StringVar(value="440")
        ttk.Entry(freq_frame, textvariable=self.freq_var).pack(side="right", fill="x", expand=True)
        
        # 波形选择
        waveform_frame = ttk.Frame(generator_frame)
        waveform_frame.pack(fill="x", pady=5)
        ttk.Label(waveform_frame, text="Waveform:").pack(side="left")
        self.waveform_var = tk.StringVar(value="sine")
        waveform_combo = ttk.Combobox(waveform_frame, textvariable=self.waveform_var)
        waveform_combo['values'] = ('sine', 'square', 'triangle', 'sawtooth')
        waveform_combo.pack(side="right", fill="x", expand=True)
        
        # 持续时间控制
        duration_frame = ttk.Frame(generator_frame)
        duration_frame.pack(fill="x", pady=5)
        ttk.Label(duration_frame, text="Duration (s):").pack(side="left")
        self.duration_var = tk.StringVar(value="1.0")
        ttk.Entry(duration_frame, textvariable=self.duration_var).pack(side="right", fill="x", expand=True)
        
        # 音量控制
        volume_frame = ttk.Frame(generator_frame)
        volume_frame.pack(fill="x", pady=5)
        ttk.Label(volume_frame, text="Volume:").pack(side="left")
        self.volume_var = tk.DoubleVar(value=0.5)
        volume_scale = ttk.Scale(volume_frame, from_=0.0, to=1.0, variable=self.volume_var, orient="horizontal")
        volume_scale.pack(side="right", fill="x", expand=True)
        
        # 生成按钮
        self.generate_button = ttk.Button(generator_frame, text="Generate Sound", command=self.toggle_generation)
        self.generate_button.pack(fill="x", pady=5)
        
        # 预览按钮
        self.preview_button = ttk.Button(generator_frame, text="Preview", command=self.toggle_preview)
        self.preview_button.pack(fill="x", pady=5)
        
        # 保存按钮
        self.save_button = ttk.Button(generator_frame, text="Save", command=self.save_generated_sound)
        self.save_button.pack(fill="x", pady=5)
        
        # 状态标签
        self.generator_status = tk.Label(generator_frame, text="Ready", font=('Arial', 14))
        self.generator_status.pack(pady=5)
        
        # 设置按钮样式
        style = ttk.Style()
        style.configure('TButton', padding=10, font=('Arial', 16))  # 增加按钮内边距和字体大小
        style.configure('TLabel', padding=5, font=('Arial', 14))  # 增加标签字体大小
        style.configure('TEntry', padding=5, font=('Arial', 14))  # 增加输入框字体大小
        style.configure('TCombobox', padding=5, font=('Arial', 14))  # 增加下拉框字体大小
        style.configure('TScale', padding=5)  # 增加滑块内边距
        style.configure('TLabelframe.Label', font=('Arial', 16))  # 增加框架标签字体大小

    def generate_waveform(self, frequency, duration, waveform='sine', amplitude=0.5):
        """生成指定波形的信号"""
        t = np.linspace(0, duration, int(self.sample_rate * duration), False)
        
        if waveform == 'sine':
            signal = amplitude * np.sin(2 * np.pi * frequency * t)
        elif waveform == 'square':
            signal = amplitude * np.sign(np.sin(2 * np.pi * frequency * t))
        elif waveform == 'triangle':
            signal = amplitude * (2/np.pi) * np.arcsin(np.sin(2 * np.pi * frequency * t))
        elif waveform == 'sawtooth':
            signal = amplitude * (2/np.pi) * np.arctan(np.tan(np.pi * frequency * t))
        else:
            signal = amplitude * np.sin(2 * np.pi * frequency * t)
            
        return signal.astype(np.float32)
    
    def toggle_generation(self):
        """切换声音生成状态"""
        if not self.generating:
            try:
                frequency = float(self.freq_var.get())
                duration = float(self.duration_var.get())
                waveform = self.waveform_var.get()
                amplitude = self.volume_var.get()
                
                self.generating = True
                self.generate_button.config(text="Stop Generation")
                self.generator_status.config(text="Generating...")
                
                # 在新线程中生成声音
                self.generator_thread = threading.Thread(
                    target=self.generate_sound,
                    args=(frequency, duration, waveform, amplitude)
                )
                self.generator_thread.start()
                
            except ValueError as e:
                self.generator_status.config(text="Invalid input parameters")
                self.generating = False
                self.generate_button.config(text="Generate Sound")
        else:
            self.stop_generation()
    
    def stop_generation(self):
        """停止声音生成"""
        self.generating = False
        self.generate_button.config(text="Generate Sound")
        self.generator_status.config(text="Generation stopped")
        
        # 停止音频流
        if self.generator_stream:
            try:
                self.generator_stream.stop()
                self.generator_stream.close()
                self.generator_stream = None
            except Exception as e:
                print(f"Error stopping generator stream: {e}")
        
        # 等待线程结束
        if self.generator_thread:
            try:
                self.generator_thread.join(timeout=1.0)
            except Exception as e:
                print(f"Error joining generator thread: {e}")
    
    def generate_sound(self, frequency, duration, waveform, amplitude):
        """生成并播放声音"""
        try:
            # 生成信号
            signal = self.generate_waveform(frequency, duration, waveform, amplitude)
            
            # 创建输出流
            self.generator_stream = sd.OutputStream(
                channels=self.channels,
                samplerate=self.sample_rate,
                callback=self.generator_callback
            )
            
            # 设置生成数据
            self.generator_data = signal
            self.generator_position = 0
            self.generator_start_time = time.time()  # 记录开始时间
            
            # 开始播放
            self.generator_stream.start()
            
            # 等待播放完成或被停止
            while self.generating and self.generator_position < len(signal):
                time.sleep(0.1)
            
            if self.generating:
                actual_duration = time.time() - self.generator_start_time  # 计算实际播放时长
                self.generator_status.config(text="Sound generated")
                self.generating = False
                self.generate_button.config(text="Generate Sound")
                self.log_sound("Generation", frequency, waveform, actual_duration, amplitude)
            
        except Exception as e:
            if self.generator_start_time:
                actual_duration = time.time() - self.generator_start_time  # 计算实际播放时长
            else:
                actual_duration = 0
            self.generator_status.config(text=f"Generation failed: {str(e)}")
            self.generating = False
            self.generate_button.config(text="Generate Sound")
            self.log_sound("Generation", frequency, waveform, actual_duration, amplitude, f"Error: {str(e)}")
        finally:
            if self.generator_stream:
                try:
                    self.generator_stream.stop()
                    self.generator_stream.close()
                    self.generator_stream = None
                except:
                    pass
    
    def generator_callback(self, outdata, frames, time, status):
        """生成声音的回调函数"""
        if status:
            print(f"Generator status: {status}")
        
        if self.generating:
            try:
                # 计算当前帧的数据
                end_position = min(self.generator_position + frames, len(self.generator_data))
                outdata[:end_position-self.generator_position, 0] = self.generator_data[self.generator_position:end_position]
                
                # 更新位置
                self.generator_position = end_position
                
            except Exception as e:
                print(f"Error in generator callback: {e}")
                self.stop_generation()
    
    def toggle_preview(self):
        """切换预览状态"""
        if not self.previewing:
            try:
                frequency = float(self.freq_var.get())
                waveform = self.waveform_var.get()
                amplitude = self.volume_var.get()
                
                self.previewing = True
                self.preview_button.config(text="Stop Preview")
                self.generator_status.config(text="Previewing...")
                
                # 在新线程中预览声音
                self.preview_thread = threading.Thread(
                    target=self.preview_sound,
                    args=(frequency, waveform, amplitude)
                )
                self.preview_thread.start()
                
            except ValueError as e:
                self.generator_status.config(text="Invalid input parameters")
                self.previewing = False
                self.preview_button.config(text="Preview")
        else:
            self.stop_preview()
    
    def stop_preview(self):
        """停止预览"""
        self.previewing = False
        self.preview_button.config(text="Preview")
        self.generator_status.config(text="Preview stopped")
        if self.preview_stream:
            try:
                self.preview_stream.stop()
                self.preview_stream.close()
                self.preview_stream = None
            except Exception as e:
                print(f"Error stopping preview stream: {e}")
        if self.preview_thread:
            try:
                self.preview_thread.join(timeout=1.0)  # 添加超时
            except Exception as e:
                print(f"Error joining preview thread: {e}")
    
    def preview_sound(self, frequency, waveform, amplitude):
        """预览生成的声音"""
        try:
            # 生成持续时间为1秒的信号，但会循环播放
            signal = self.generate_waveform(frequency, 1.0, waveform, amplitude)
            
            # 创建输出流
            self.preview_stream = sd.OutputStream(
                channels=self.channels,
                samplerate=self.sample_rate,
                callback=self.preview_callback
            )
            
            # 设置预览数据
            self.preview_data = signal
            self.preview_position = 0
            self.preview_start_time = time.time()  # 记录开始时间
            
            # 开始播放
            self.preview_stream.start()
            
            # 等待被停止
            while self.previewing:
                time.sleep(0.1)
            
            if self.preview_stream:
                self.preview_stream.stop()
                self.preview_stream.close()
                self.preview_stream = None
            
            actual_duration = time.time() - self.preview_start_time  # 计算实际播放时长
            self.preview_button.config(text="Preview")
            self.generator_status.config(text="Preview stopped")
            self.log_sound("Preview", frequency, waveform, actual_duration, amplitude)
            
        except Exception as e:
            if self.preview_start_time:
                actual_duration = time.time() - self.preview_start_time  # 计算实际播放时长
            else:
                actual_duration = 0
            self.generator_status.config(text=f"Preview failed: {str(e)}")
            self.previewing = False
            self.preview_button.config(text="Preview")
            self.log_sound("Preview", frequency, waveform, actual_duration, amplitude, f"Error: {str(e)}")
        finally:
            if self.preview_stream:
                try:
                    self.preview_stream.stop()
                    self.preview_stream.close()
                    self.preview_stream = None
                except:
                    pass
    
    def preview_callback(self, outdata, frames, time, status):
        """预览回调函数"""
        if status:
            print(f"Preview status: {status}")
        
        if self.previewing:
            try:
                # 循环播放信号
                for i in range(frames):
                    outdata[i, 0] = self.preview_data[self.preview_position]
                    self.preview_position = (self.preview_position + 1) % len(self.preview_data)
            except Exception as e:
                print(f"Error in preview callback: {e}")
                self.stop_preview()
    
    def save_generated_sound(self):
        """保存生成的声音"""
        try:
            frequency = float(self.freq_var.get())
            duration = float(self.duration_var.get())
            waveform = self.waveform_var.get()
            amplitude = self.volume_var.get()
            
            signal = self.generate_waveform(frequency, duration, waveform, amplitude)
            
            # 生成文件名
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"generated_{waveform}_{frequency}Hz_{duration}s_{timestamp}.wav"
            
            # 保存文件
            with wave.open(os.path.join(self.recordings_dir, filename), 'w') as wf:
                wf.setnchannels(self.channels)
                wf.setsampwidth(2)  # 16-bit
                wf.setframerate(self.sample_rate)
                wf.writeframes((signal * 32767).astype(np.int16).tobytes())
            
            self.generator_status.config(text=f"Sound saved: {filename}")
            
        except Exception as e:
            self.generator_status.config(text=f"Save failed: {str(e)}")
    
    def on_closing(self):
        """窗口关闭时的处理"""
        self.is_closing = True  # 设置关闭标志
        
        # 停止所有正在进行的操作
        if self.recording:
            self.stop_recording()
        if self.previewing:
            self.stop_preview()
        if self.generating:
            self.stop_generation()
        
        # 清理资源
        if self.stream:
            try:
                self.stream.stop()
                self.stream.close()
            except:
                pass
        if self.preview_stream:
            try:
                self.preview_stream.stop()
                self.preview_stream.close()
            except:
                pass
        if self.generator_stream:  # 添加生成流的清理
            try:
                self.generator_stream.stop()
                self.generator_stream.close()
            except:
                pass
        
        # 取消所有定时器
        if self.update_plot_timer:
            try:
                self.root.after_cancel(self.update_plot_timer)
            except:
                pass
        
        # 关闭窗口
        self.root.quit()
        self.root.destroy()
    
    def run(self):
        """运行录音程序"""
        print("\n=== Audio Recording Tool ===")
        print("Click buttons to control recording")
        print("Press Ctrl+C to exit")
        print("\nWaiting to start recording...")
        
        try:
            self.root.mainloop()
        except KeyboardInterrupt:
            if self.recording:
                self.stop_recording()
            self.root.quit()
            print("\nProgram terminated")

def main():
    recorder = AudioRecorder()
    recorder.run()

if __name__ == "__main__":
    main() 