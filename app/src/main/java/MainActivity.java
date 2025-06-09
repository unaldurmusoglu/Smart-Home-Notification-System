package com.example.arduinosecuritysystem;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Set;
import java.util.UUID;

public class MainActivity extends AppCompatActivity {

    private static final String TAG = "ArduinoSecuritySystem";
    private static final int REQUEST_ENABLE_BT = 1;
    private static final int REQUEST_LOCATION_PERMISSION = 2;

    // UI Bileşenleri
    private TextView connectionStatusTextView;
    private TextView alarmStatusTextView;
    private TextView doorStatusTextView;
    private TextView alertStatusTextView;
    private Button connectButton;
    private Button statusButton;
    private Button armButton;
    private Button disarmButton;
    private Button lockButton;
    private Button unlockButton;
    private Button stopAlarmButton;
    private Button sendPasswordButton;
    private EditText passwordEditText;
    private ListView notificationsListView;

    // Bluetooth Değişkenleri
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothSocket bluetoothSocket;
    private OutputStream outputStream;
    private InputStream inputStream;
    private Thread workerThread;
    private byte[] readBuffer;
    private int readBufferPosition;
    private volatile boolean stopWorker;

    // Bildirimler için ArrayList ve Adapter
    private ArrayList<String> notificationsList;
    private ArrayAdapter<String> notificationsAdapter;

    // Standard SPP UUID
    private static final UUID BT_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // UI Bileşenlerini tanımlama
        connectionStatusTextView = findViewById(R.id.connectionStatusTextView);
        alarmStatusTextView = findViewById(R.id.alarmStatusTextView);
        doorStatusTextView = findViewById(R.id.doorStatusTextView);
        alertStatusTextView = findViewById(R.id.alertStatusTextView);
        connectButton = findViewById(R.id.connectButton);
        statusButton = findViewById(R.id.statusButton);
        armButton = findViewById(R.id.armButton);
        disarmButton = findViewById(R.id.disarmButton);
        lockButton = findViewById(R.id.lockButton);
        unlockButton = findViewById(R.id.unlockButton);
        stopAlarmButton = findViewById(R.id.stopAlarmButton);
        sendPasswordButton = findViewById(R.id.sendPasswordButton);
        passwordEditText = findViewById(R.id.passwordEditText);
        notificationsListView = findViewById(R.id.notificationsListView);

        // Bluetooth adaptörünü başlatma
        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();

        // Cihazda Bluetooth olmama durumu
        if (bluetoothAdapter == null) {
            Toast.makeText(this, "Bu cihaz Bluetooth desteklemiyor", Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        // Bildirim listesini başlatma
        notificationsList = new ArrayList<>();
        notificationsAdapter = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, notificationsList);
        notificationsListView.setAdapter(notificationsAdapter);

        // Butonlara tıklama işlevlerini ekleme
        connectButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                checkBluetoothPermissions();
            }
        });

        statusButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                sendCommand("STATUS\n");
            }
        });

        armButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                sendCommand("ARM\n");
            }
        });

        disarmButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                sendCommand("DISARM\n");
            }
        });

        lockButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                sendCommand("LOCK\n");
            }
        });

        unlockButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                sendCommand("UNLOCK\n");
            }
        });

        stopAlarmButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                sendCommand("STOP\n");
            }
        });

        sendPasswordButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                String password = passwordEditText.getText().toString();
                if (!password.isEmpty()) {
                    sendCommand("PASSWORD:" + password + "\n");
                    passwordEditText.setText("");
                } else {
                    Toast.makeText(MainActivity.this, "Şifre boş olamaz!", Toast.LENGTH_SHORT).show();
                }
            }
        });
    }

    private void checkBluetoothPermissions() {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
            // Android 12+ için yeni Bluetooth izinleri
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED ||
                    ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {

                ActivityCompat.requestPermissions(this,
                        new String[]{
                                Manifest.permission.BLUETOOTH_CONNECT,
                                Manifest.permission.BLUETOOTH_SCAN
                        },
                        REQUEST_ENABLE_BT);
                return;
            }
        } else {
            // Eski Android sürümleri için izinler
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this,
                        new String[]{Manifest.permission.ACCESS_FINE_LOCATION},
                        REQUEST_LOCATION_PERMISSION);
                return;
            }
        }

        // Bluetooth açık değilse aç
        if (!bluetoothAdapter.isEnabled()) {
            Intent enableBtIntent = new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE);
            if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED) {
                startActivityForResult(enableBtIntent, REQUEST_ENABLE_BT);
            }
            return;
        }

        // Bluetooth açıksa cihazları listele
        listPairedDevices();
    }

    private void listPairedDevices() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            Toast.makeText(this, "Bluetooth izni eksik!", Toast.LENGTH_SHORT).show();
            return;
        }

        Set<BluetoothDevice> pairedDevices = bluetoothAdapter.getBondedDevices();

        if (pairedDevices.size() > 0) {
            final ArrayList<String> deviceNames = new ArrayList<>();
            final ArrayList<BluetoothDevice> devices = new ArrayList<>();

            for (BluetoothDevice device : pairedDevices) {
                deviceNames.add(device.getName() + "\n" + device.getAddress());
                devices.add(device);
            }

            final AlertDialog.Builder builder = new AlertDialog.Builder(this);
            builder.setTitle("Eşleştirilmiş Cihazlar");

            builder.setItems(deviceNames.toArray(new String[0]), new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialog, int which) {
                    connectToDevice(devices.get(which));
                }
            });

            builder.setNegativeButton("İptal", null);
            builder.setCancelable(true);
            builder.show();
        } else {
            Toast.makeText(this, "Eşleştirilmiş cihaz bulunamadı!", Toast.LENGTH_SHORT).show();
        }
    }

    private void connectToDevice(BluetoothDevice device) {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            Toast.makeText(this, "Bluetooth izni eksik!", Toast.LENGTH_SHORT).show();
            return;
        }

        Toast.makeText(this, device.getName() + " cihazına bağlanılıyor...", Toast.LENGTH_SHORT).show();

        // Önceki bağlantıyı kapat
        if (bluetoothSocket != null) {
            try {
                bluetoothSocket.close();
            } catch (IOException e) {
                Log.e(TAG, "Önceki bağlantı kapatılamadı: " + e.getMessage());
            }
            bluetoothSocket = null;
        }

        try {
            bluetoothSocket = device.createRfcommSocketToServiceRecord(BT_UUID);
            bluetoothSocket.connect();

            outputStream = bluetoothSocket.getOutputStream();
            inputStream = bluetoothSocket.getInputStream();

            connectionStatusTextView.setText("Bağlantı Durumu: Bağlı (" + device.getName() + ")");
            connectionStatusTextView.setTextColor(getResources().getColor(android.R.color.hb_green));

            sendCommand("STATUS\n"); // Bağlantı sonrası durum sorgulama

            // Veri alma işlemini başlat
            startDataReception();

            Toast.makeText(this, device.getName() + " cihazına bağlantı başarılı!", Toast.LENGTH_SHORT).show();

        } catch (IOException e) {
            Log.e(TAG, "Bağlantı hatası: " + e.getMessage());
            Toast.makeText(this, "Bağlantı başarısız: " + e.getMessage(), Toast.LENGTH_SHORT).show();
            connectionStatusTextView.setText("Bağlantı Durumu: Bağlı Değil (Hata)");
            connectionStatusTextView.setTextColor(getResources().getColor(android.R.color.holo_red_dark));
        }
    }

    private void startDataReception() {
        stopWorker = false;
        readBufferPosition = 0;
        readBuffer = new byte[1024];

        workerThread = new Thread(new Runnable() {
            @Override
            public void run() {
                while (!Thread.currentThread().isInterrupted() && !stopWorker) {
                    try {
                        int bytesAvailable = inputStream.available();

                        if (bytesAvailable > 0) {
                            byte[] packetBytes = new byte[bytesAvailable];
                            inputStream.read(packetBytes);

                            for (int i = 0; i < bytesAvailable; i++) {
                                byte b = packetBytes[i];

                                if (b == '\n') {
                                    byte[] encodedBytes = new byte[readBufferPosition];
                                    System.arraycopy(readBuffer, 0, encodedBytes, 0, encodedBytes.length);
                                    final String data = new String(encodedBytes, "UTF-8");
                                    readBufferPosition = 0;

                                    Handler handler = new Handler(Looper.getMainLooper());
                                    handler.post(new Runnable() {
                                        @Override
                                        public void run() {
                                            processReceivedData(data);
                                        }
                                    });
                                } else {
                                    readBuffer[readBufferPosition++] = b;
                                }
                            }
                        }
                    } catch (IOException ex) {
                        stopWorker = true;
                        break;
                    }
                }
            }
        });

        workerThread.start();
    }

    private void processReceivedData(String data) {
        Log.d(TAG, "Alınan Veri: " + data);

        // Boş verileri atla
        if (data.trim().isEmpty()) {
            return;
        }

        // Durum bilgilerini güncelle
        if (data.startsWith("Alarm:")) {
            alarmStatusTextView.setText(data);
        } else if (data.startsWith("Door:")) {
            doorStatusTextView.setText(data);
        } else if (data.startsWith("Alert:")) {
            alertStatusTextView.setText(data);
        } else if (data.startsWith("[ALERT]")) {
            // Bildirimleri listeye ekle ve kaydır
            notificationsList.add(0, data);
            notificationsAdapter.notifyDataSetChanged();
        }
    }

    private void sendCommand(String command) {
        if (bluetoothSocket == null || outputStream == null) {
            Toast.makeText(this, "Önce cihaza bağlanın!", Toast.LENGTH_SHORT).show();
            return;
        }

        try {
            outputStream.write(command.getBytes());
            Log.d(TAG, "Gönderilen Komut: " + command.trim());
        } catch (IOException e) {
            Log.e(TAG, "Komut gönderilemedi: " + e.getMessage());
            Toast.makeText(this, "Komut gönderilemedi: " + e.getMessage(), Toast.LENGTH_SHORT).show();

            // Bağlantı kesilmiş olabilir, durumu güncelle
            connectionStatusTextView.setText("Bağlantı Durumu: Bağlantı Kesildi");
            connectionStatusTextView.setTextColor(getResources().getColor(android.R.color.holo_red_dark));
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        // Bağlantıyı kapat
        if (bluetoothSocket != null) {
            try {
                stopWorker = true;
                if (workerThread != null) {
                    workerThread.interrupt();
                }
                outputStream.close();
                inputStream.close();
                bluetoothSocket.close();
            } catch (IOException e) {
                Log.e(TAG, "Bağlantı kapatılamadı: " + e.getMessage());
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        switch (requestCode) {
            case REQUEST_ENABLE_BT:
            case REQUEST_LOCATION_PERMISSION:
                if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                    checkBluetoothPermissions();
                } else {
                    Toast.makeText(this, "İzin reddedildi, uygulama düzgün çalışmayabilir", Toast.LENGTH_LONG).show();
                }
                break;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == REQUEST_ENABLE_BT) {
            if (resultCode == RESULT_OK) {
                listPairedDevices();
            } else {
                Toast.makeText(this, "Bluetooth etkinleştirilmedi, uygulama düzgün çalışmayabilir", Toast.LENGTH_LONG).show();
            }
        }
    }
}