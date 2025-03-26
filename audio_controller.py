import sounddevice as sd
import numpy as np
import wave
import time
from datetime import datetime
import threading
import os
import csv
import pandas as pd

class AudioManager:
    def __init__(self):
        # 采样率 / Sample rate
        self.sample_rate = 44100
        self.recording = False
        self.playing = False
        self.recorded_data = []
        
        # 创建必要的目录 / Create necessary directories
        self.recordings_dir = "recordings"
        self.logs_dir = "logs"
        for directory in [self.recordings_dir, self.logs_dir]:
            if not os.path.exists(directory):
                os.makedirs(directory)
        
        # 初始化日志文件 / Initialize log file
        self.log_file = os.path.join(self.logs_dir, f"audio_log_{datetime.now().strftime('%Y%m%d')}.csv")
        self.initialize_log_file()
    
    def initialize_log_file(self):
        """初始化CSV日志文件 / Initialize CSV log file"""
        if not os.path.exists(self.log_file):
            with open(self.log_file, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow([
                    'Timestamp',
                    'Operation',
                    'Frequency',
                    'Duration',
                    'Filename',
                    'Status',
                    'Error'
                ])

    def log_operation(self, operation, frequency=None, duration=None, filename=None, status="Success", error=None):
        """记录操作到CSV / Log operation to CSV"""
        with open(self.log_file, 'a', newline='') as f:
            writer = csv.writer(f)
            writer.writerow([
                datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                operation,
                frequency if frequency is not None else "",
                duration if duration is not None else "",
                filename if filename is not None else "",
                status,
                error if error is not None else ""
            ])

    def generate_waveform(self, frequency, duration, waveform='sine', amplitude=0.5):
        """生成指定波形的信号 / Generate signal with specified waveform"""
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
    
    def play_tone(self, frequency, duration, waveform='sine'):
        """播放指定波形的声音 / Play tone with specified waveform"""
        try:
            tone = self.generate_waveform(frequency, duration, waveform)
            self.playing = True
            sd.play(tone, self.sample_rate)
            sd.wait()  # 等待播放完成 / Wait for playback to complete
            self.playing = False
            self.log_operation("Play", frequency=frequency, duration=duration)
            
        except Exception as e:
            error_msg = str(e)
            print(f"Error playing sound: {error_msg}")
            self.log_operation("Play", frequency=frequency, duration=duration, 
                             status="Failed", error=error_msg)
            self.playing = False
    
    def record_audio(self, duration, filename):
        """录制音频 / Record audio"""
        try:
            print(f"Starting recording, duration: {duration} seconds")
            self.recording = True
            
            # 录制音频 / Record audio
            recording = sd.rec(
                int(duration * self.sample_rate),
                samplerate=self.sample_rate,
                channels=1
            )
            sd.wait()  # 等待录制完成 / Wait for recording to complete
            self.recording = False
            
            # 完整的文件路径 / Complete file path
            filepath = os.path.join(self.recordings_dir, filename)
            
            # 保存录音 / Save recording
            with wave.open(filepath, 'wb') as wf:
                wf.setnchannels(1)  # 单声道 / Mono channel
                wf.setsampwidth(2)  # 2字节采样宽度 / 2-byte sample width
                wf.setframerate(self.sample_rate)
                wf.writeframes((recording * 32767).astype(np.int16).tobytes())
            
            print(f"Recording completed, file saved: {filepath}")
            self.log_operation("Record", duration=duration, filename=filename)
            
        except Exception as e:
            error_msg = str(e)
            print(f"Error recording audio: {error_msg}")
            self.log_operation("Record", duration=duration, filename=filename, 
                             status="Failed", error=error_msg)
            self.recording = False
    
    def play_and_record(self, frequency, duration, waveform='sine'):
        """同时播放和录制音频 / Simultaneously play and record audio"""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"audio_record_{timestamp}_{frequency}Hz_{waveform}.wav"
        
        try:
            # 创建录制线程 / Create recording thread
            record_thread = threading.Thread(
                target=self.record_audio,
                args=(duration, filename)
            )
            
            # 创建播放线程 / Create playback thread
            play_thread = threading.Thread(
                target=self.play_tone,
                args=(frequency, duration, waveform)
            )
            
            # 启动录制 / Start recording
            record_thread.start()
            time.sleep(0.5)  # 等待录制启动 / Wait for recording to start
            
            # 启动播放 / Start playback
            play_thread.start()
            
            # 等待两个线程完成 / Wait for both threads to complete
            play_thread.join()
            record_thread.join()
            
            self.log_operation("PlayAndRecord", frequency=frequency, 
                             duration=f"play:{duration},waveform:{waveform}", 
                             filename=filename)
            
        except Exception as e:
            error_msg = str(e)
            print(f"Error during play and record: {error_msg}")
            self.log_operation("PlayAndRecord", frequency=frequency, 
                             duration=f"play:{duration},waveform:{waveform}", 
                             filename=filename, status="Failed", error=error_msg)

def main():
    # 创建音频管理器实例 / Create audio manager instance
    audio_mgr = AudioManager()
    
    try:
        while True:
            print("\n=== Audio Generator and Recorder ===")
            print("Available waveforms: sine, square, triangle, sawtooth")
            print("Enter 'q' to quit")
            
            waveform = input("\nSelect waveform (default: sine): ").lower()
            if waveform == 'q':
                break
            if not waveform:
                waveform = 'sine'
            
            if waveform not in ['sine', 'square', 'triangle', 'sawtooth']:
                print("Invalid waveform, using sine wave")
                waveform = 'sine'
            
            try:
                freq = float(input("Enter frequency (Hz): "))
                duration = float(input("Enter duration (seconds): "))
                
                print(f"\nPlaying and recording {freq}Hz {waveform} wave...")
                audio_mgr.play_and_record(freq, duration, waveform)
                
            except ValueError:
                print("Invalid input. Please enter numeric values.")
                
    except KeyboardInterrupt:
        print("\nProgram interrupted by user")
    except Exception as e:
        print(f"An error occurred: {e}")
    finally:
        # 确保所有音频操作都已停止 / Ensure all audio operations are stopped
        sd.stop()

if __name__ == "__main__":
    main() 