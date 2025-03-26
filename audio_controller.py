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

    def generate_tone(self, frequency, duration, amplitude=0.5):
        """生成指定频率的正弦波 / Generate sine wave with specified frequency"""
        t = np.linspace(0, duration, int(self.sample_rate * duration), False)
        tone = amplitude * np.sin(2 * np.pi * frequency * t)
        return tone.astype(np.float32)
    
    def play_tone(self, frequency, duration):
        """播放指定频率的声音 / Play tone with specified frequency"""
        try:
            tone = self.generate_tone(frequency, duration)
            self.playing = True
            sd.play(tone, self.sample_rate)
            sd.wait()
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
            
            # 设置录音参数 / Set recording parameters
            channels = 1
            recording = sd.rec(
                int(duration * self.sample_rate),
                samplerate=self.sample_rate,
                channels=channels,
                dtype=np.int16  # 改用int16类型 / Use int16 type
            )
            
            # 等待录制完成 / Wait for recording to complete
            sd.wait()
            self.recording = False
            
            # 保存录音 / Save recording
            filepath = os.path.join(self.recordings_dir, filename)
            with wave.open(filepath, 'wb') as wf:
                wf.setnchannels(channels)
                wf.setsampwidth(2)  # 16位采样 / 16-bit sampling
                wf.setframerate(self.sample_rate)
                wf.writeframes(recording.tobytes())
            
            print(f"Recording completed, file saved: {filepath}")
            self.log_operation("Record", duration=duration, filename=filename)
            
        except Exception as e:
            error_msg = str(e)
            print(f"Error recording audio: {error_msg}")
            self.log_operation("Record", duration=duration, filename=filename, 
                             status="Failed", error=error_msg)
            self.recording = False
    
    def play_and_record(self, frequency, duration, record_duration=None):
        """同时播放和录制音频 / Simultaneously play and record audio"""
        if record_duration is None:
            record_duration = duration + 1
            
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"audio_record_{timestamp}_{frequency}Hz.wav"
        
        try:
            record_thread = threading.Thread(
                target=self.record_audio,
                args=(record_duration, filename)
            )
            
            play_thread = threading.Thread(
                target=self.play_tone,
                args=(frequency, duration)
            )
            
            record_thread.start()
            time.sleep(0.5)
            play_thread.start()
            
            play_thread.join()
            record_thread.join()
            
            self.log_operation("PlayAndRecord", frequency=frequency, 
                             duration=f"play:{duration},record:{record_duration}", 
                             filename=filename)
            
        except Exception as e:
            error_msg = str(e)
            print(f"Error during play and record: {error_msg}")
            self.log_operation("PlayAndRecord", frequency=frequency, 
                             duration=f"play:{duration},record:{record_duration}", 
                             filename=filename, status="Failed", error=error_msg)

def main():
    # 创建音频管理器实例 / Create audio manager instance
    audio_mgr = AudioManager()
    
    try:
        while True:
            print("\n=== Audio Playback and Recording System ===")
            print("1. Play tone with specified frequency")
            print("2. Record environmental sound")
            print("3. Play and record simultaneously")
            print("4. Exit")
            
            choice = input("\nSelect operation (1-4): ")
            
            if choice == '1':
                freq = float(input("Enter frequency (Hz): "))
                duration = float(input("Enter duration (seconds): "))
                print(f"\nPlaying {freq}Hz tone...")
                audio_mgr.play_tone(freq, duration)
                
            elif choice == '2':
                duration = float(input("Enter recording duration (seconds): "))
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                filename = f"environment_record_{timestamp}.wav"
                audio_mgr.record_audio(duration, filename)
                
            elif choice == '3':
                freq = float(input("Enter frequency (Hz): "))
                duration = float(input("Enter playback duration (seconds): "))
                record_duration = float(input("Enter recording duration (seconds): "))
                print(f"\nPlaying {freq}Hz tone and recording...")
                audio_mgr.play_and_record(freq, duration, record_duration)
                
            elif choice == '4':
                print("\nExiting program")
                break
                
            else:
                print("\nInvalid choice, please try again")
                
    except KeyboardInterrupt:
        print("\nProgram interrupted by user")
    except Exception as e:
        print(f"An error occurred: {e}")
    finally:
        # 确保所有音频操作都已停止 / Ensure all audio operations are stopped
        sd.stop()

if __name__ == "__main__":
    main() 