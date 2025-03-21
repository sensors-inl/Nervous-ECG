/* Copyright (c) 2024 INSA Lyon, CNRS, INL UMR 5270 */
/* This file is under MIT Licence */
/* Full text available at https://mit-license.org/ */

/* Compatibility check */
const userAgent = navigator.userAgent.toLowerCase();
console.log("Application is runnning on " + userAgent);
const isSafari = /^((?!chrome|android).)*safari/i.test(userAgent);
const isCompatible = (/chrome|edg|opera/i.test(userAgent)) && !isSafari;
if (!isCompatible) {
  alert("Applications require Chrome, Edge or Opera web browsers");
}


/*******************************************************************************
 * BLE connection and RX data handler
 ******************************************************************************/

/* Graphical components binding */
const connectBLEButton = document.querySelector('.app-ble-connect-button');
const connectBLEButtonRipple = new mdc.ripple.MDCRipple(connectBLEButton);
const connectBLEButtonLabel = document.querySelector('.app-ble-connect-button-label');
const connectBLEStatusIcon = document.querySelector('.app-ble-status-icon');

/* Global variables */
const UUIDS = {
    NRF_UART_SERVICE_UUID: '6e400001-b5a3-f393-e0a9-e50e24dcca9e',
    NRF_UART_TX_CHAR_UUID: '6e400002-b5a3-f393-e0a9-e50e24dcca9e',
    NRF_UART_RX_CHAR_UUID: '6e400003-b5a3-f393-e0a9-e50e24dcca9e',
};
const bleDeviceNamePrefix = "ECG"
let bleDevice;
let bleDeviceName;
let bleConnected = false;


async function onConnectBLEButtonClick() {
    if (bleConnected == false) {
        disableAllButtons();
        connectBLEButtonLabel.innerHTML = 'Connecting...'
        let searchTimer = setInterval(
            function () {
                if (connectBLEStatusIcon.innerHTML == 'bluetooth_searching') {
                    connectBLEStatusIcon.innerHTML = 'bluetooth';
                }
                else {
                    connectBLEStatusIcon.innerHTML = 'bluetooth_searching';
                }
            },
            750
        );
        try {
            bleDevice = await navigator.bluetooth.requestDevice({
                filters: [
                    { namePrefix: bleDeviceNamePrefix },
                ],
                optionalServices: [UUIDS.NRF_UART_SERVICE_UUID, 'device_information', 'battery_service'],
            });
            bleDevice.addEventListener('gattserverdisconnected', bleOnDisconnected);
            const gatt = await bleDevice.gatt.connect();
            // Get DIS characteristics
            const dis = await gatt.getPrimaryService('device_information');
            const versionChar = await dis.getCharacteristic('firmware_revision_string');
            const versionValue = await versionChar.readValue();
            let decoder = new TextDecoder('utf-8');
            const versionValueString = decoder.decode(versionValue);
            // Get BAS characteristics
            const batService = await gatt.getPrimaryService('battery_service');
            const batChar = await batService.getCharacteristic('battery_level');
            // Get NUS characteristics
            const nusService = await gatt.getPrimaryService(UUIDS.NRF_UART_SERVICE_UUID);
            const rxChar = await nusService.getCharacteristic(UUIDS.NRF_UART_RX_CHAR_UUID);
            const txChar = await nusService.getCharacteristic(UUIDS.NRF_UART_TX_CHAR_UUID);
            bleSetupRxListener(rxChar);
            bleSetupBatListener(batChar);
            window.rxChar = rxChar;
            window.txChar = txChar;
            window.batChar = batChar;
            window.batChar.startNotifications();
            console.log("Connected");
            bleConnected = true;
            clearInterval(searchTimer);
            enableControlButtons();
            connectBLEButtonLabel.innerHTML = 'Disconnect BLE';
            connectBLEStatusIcon.innerHTML = 'bluetooth_connected';
            connectBLEButton.removeAttribute('disabled');
            connectBLEStatusIcon.removeAttribute('disabled');
            bleDeviceName = bleDevice.name;
            deviceLabel.innerHTML = 'Device: ' + bleDeviceName + " v" + versionValueString;
            getDeviceInformation();
        }
        catch (err) {
            console.error("BLE Connection error " + err);
            alert("Unable to connect");
            clearInterval(searchTimer);
            connectBLEButtonLabel.innerHTML = 'Connect BLE';
            connectBLEStatusIcon.innerHTML = 'bluetooth';
            connectBLEButton.removeAttribute('disabled');
        }
    }
    else {
        bleDevice.gatt.disconnect();
    }
}

function bleOnDisconnected(event) {
    console.log("Disconnected");
    bleConnected = false;
    disableAllButtons();
    connectBLEButtonLabel.innerHTML = 'Connect BLE';
    connectBLEStatusIcon.innerHTML = 'bluetooth';
    connectBLEButton.removeAttribute('disabled');
    connectBLEStatusIcon.setAttribute('disabled', '');
    deviceLabel.innerHTML = 'Device';
}

function bleSetupRxListener(rxchar) {
    let rx_buf = [];

    /** This is the main RX callback for NUS. It handles sliced messages before calling appropriate callback */
    rxchar.addEventListener("characteristicvaluechanged",
        /** @param {Bluetooth} e */
        (e) => {
            /** @type {BluetoothRemoteGATTCharacteristic} */
            const char = e.target;
            let rx_data = new Uint8Array(char.value.buffer);
            rx_buf = new Uint8Array([...rx_buf,...rx_data]);
            let zeroIndex = rx_buf.indexOf(0);
            if(zeroIndex != -1) {
                const cobs_data = rx_buf.slice(0, zeroIndex + 1);
                decodeMessage(cobs_data);
                rx_buf = rx_buf.slice(zeroIndex + 1);
            }

        }
    );
}

function bleSetupBatListener(batchar) {
    batchar.addEventListener("characteristicvaluechanged",
        /** @param {Bluetooth} e */
        (e) => {
            /** @type {BluetoothRemoteGATTCharacteristic} */
            const char = e.target;
            const rx_data = new Uint8Array(char.value.buffer);
            const batteryLevel = rx_data[0];
            console.log("Battery " + batteryLevel +"%");
            updateViewBattery(batteryLevel);
        }
    );
}

/*******************************************************************************
 * RX Message decoder
 ******************************************************************************/

/* Global variables */
const samplingFrequency = 512; // Hertz

/**
 * @param {Uint8Array} message
 */
function decodeMessage(message) {
    try {
        const decoded = decode(message).subarray(0,-1);
        const ecgBuffer = proto.EcgBuffer.deserializeBinary(decoded);
        const data = ecgBuffer.getData_asU8();
        const dataBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
        const int16Data = new Int16Array(dataBuffer);
        const timestamp = ecgBuffer.getTimestamp();
        const time = (timestamp.getTime() + (timestamp.getUs() * 10**-6)) - timeDataStart;
        const timeArray = makeArr(time, 1.0 / samplingFrequency, int16Data.length);
        ecgChartAddData(int16Data, timeArray);
    } catch (error) {
        console.error("Error while decoding message: " + error);
    }
}

/* Helper to create lineary spaced data array */
/**
 * 
 * @param {Number} startValue 
 * @param {Number} step 
 * @param {Number} cardinality 
 * @returns 
 */
function makeArr(startValue, step, cardinality) {
    var arr = [];
    for (var i = 0; i < cardinality; i++) {
        arr.push(startValue + (step * i));
    }
    return arr;
}

/*******************************************************************************
 * TX Message encoder
 ******************************************************************************/

/**
 * @param {proto.Timestamp} timestamp
 */
async function encodeMessage(timestamp) {
    /* Serialize JS object */
    let protoBuffer = timestamp.serializeBinary();
    /* Encode with COBS */
    let cobsBuffer = encode(protoBuffer);
    /* Add final zero for subsequent decoding by nanocobs */
    cobsBuffer = new Uint8Array([...cobsBuffer, 0]);
    /* Dispatch to interface */
    if (bleConnected == true) {
        await txChar.writeValueWithoutResponse(cobsBuffer);
    }
    else {
        console.error("No device connected to send request");
    }
}

/*******************************************************************************
 * Crossed interface controls
 ******************************************************************************/


/* Graphical components binding */
const startMeasureButton = document.querySelector('.app-start-measure-button');
const startMeasureButtonRipple = new mdc.ripple.MDCRipple(startMeasureButton);
const stopMeasureButton = document.querySelector('.app-stop-measure-button');
const stopMeasureButtonRipple = new mdc.ripple.MDCRipple(stopMeasureButton);
const batteryStatusIcon = document.querySelector('.app-bat-status-icon');
const deviceLabel = document.getElementById('device-title-id');

function disableControlButtons() {
    startMeasureButton.setAttribute('disabled', '');
    stopMeasureButton.setAttribute('disabled', '');
    batteryStatusIcon.setAttribute('disabled', '');
}

function enableControlButtons() {
    startMeasureButton.removeAttribute('disabled');
    stopMeasureButton.removeAttribute('disabled');
    batteryStatusIcon.removeAttribute('disabled');
}

function disableAllButtons() {
    disableControlButtons();
    connectBLEButton.setAttribute('disabled', '');
}

/**
 * @param {number} level 
 */
function updateViewBattery(level) {
    if (level > 90) {
        batteryStatusIcon.innerHTML = 'battery_full';
    }
    else if (level > 80) {
        batteryStatusIcon.innerHTML = 'battery_6_bar';
    }
    else if (level > 70) {
        batteryStatusIcon.innerHTML = 'battery_5_bar';
    }
    else if (level > 60) {
        batteryStatusIcon.innerHTML = 'battery_4_bar';
    }
    else if (level > 50) {
        batteryStatusIcon.innerHTML = 'battery_3_bar';
    }
    else if (level > 40) {
        batteryStatusIcon.innerHTML = 'battery_2_bar';
    }
    else if (level > 30) {
        batteryStatusIcon.innerHTML = 'battery_1_bar';
    }
    else if (level > 20) {
        batteryStatusIcon.innerHTML = 'battery_0_bar';
    }
    else {
        batteryStatusIcon.innerHTML = 'battery_alert';
    }
}

async function onStartMeasureButtonClick() {
    if (bleConnected == false) return;
    // Save start time
    let millis = Date.now();
    timeDataStart = millis * 0.001;
    // Placeholder to save data in file later
    window.data = [];
    // Enable notifications
    window.rxChar.startNotifications();
    // Reset graphes
    onClearGraphClick();
}

async function onStopMeasureButtonClick() {
    if (bleConnected == false) return;
    // Disable notifications
    window.rxChar.stopNotifications();
    // Save data
    saveWindowData();
}

async function setDeviceTimestamp() {
    const date = Date.now();
    const seconds = Math.floor(Date.now() * 1e-3);
    const micros = (date - (seconds * 1e3)) * 1e3;
    const timestamp = new proto.Timestamp()
        .setTime(seconds)
        .setUs(micros);
    await encodeMessage(timestamp);
}

async function getDeviceBattery() {
    await batChar.readValue().then(value => {
        let batteryLevel = value.getUint8(0);
        console.log("Battery " + batteryLevel +"%");
        updateViewBattery(batteryLevel);
    })
}

function getDeviceInformation() {
    setTimeout(setDeviceTimestamp, 200);
    setTimeout(getDeviceBattery, 400);
}

/**
 * @param {number} timestamp
 * @returns {string} formattedTime
 */
function getFormattedTime(timestamp) {
    let date = new Date(timestamp * 1000);
    let year = date.getFullYear();
    let month = "0" + (date.getMonth() + 1);
    let day = "0" + date.getDate();
    let hours = "0" + date.getHours();
    let minutes = "0" + date.getMinutes();
    let seconds = "0" + date.getSeconds();
    let formattedTime = year + '-' + month.substr(-2) + '-' + day.substr(-2) + ' ' + hours.substr(-2) + ':' + minutes.substr(-2) + ':' + seconds.substr(-2);
    return formattedTime;
}

/*******************************************************************************
 * Data Plots
 ******************************************************************************/

/*** Global variables ***/
Chart.defaults.font.family = "'Roboto', 'Verdana'";
Chart.defaults.font.size = 12;
const chartTimeMax = 10.0; // seconds
let timeDataStart = 0.0;
const colorset = [ '#ef5350FF', '#5c6bc0FF', '#26a69aFF', '#ffee58FF', '#5d4037ff', '#e91e63ff', '#2196f3ff', '#4caf50ff', '#ffc107ff', '#ef5350FF', '#5c6bc0FF', '#26a69aFF', '#ffee58FF', '#5d4037ff', '#e91e63ff', '#2196f3ff', '#4caf50ff', '#ffc107ff'];
const EDA_FREQUENCY_LIST = [12, 28, 32, 36, 44, 68, 84, 108, 136, 196, 256, 324, 400, 484, 576, 726];
window.data = [];

/* Graphical components binding */

const clearGraphButton = document.querySelector('.app-clear-graph-button');
const clearGraphButtonRipple = new mdc.ripple.MDCRipple(clearGraphButton);

const ecgChart = new Chart(document.getElementById('app-ecg-chart-canvas'), {
    type: 'scatter',
    data: {
        labels: [],
        datasets: [{
            label: "ECG", 
            labels: [], 
            data: [], 
            tension: 0, 
            backgroundColor: colorset[0], 
            borderColor: colorset[0], 
            showLine: true,
        }],
    },
    options: {
        pointRadius: 0,
        animation: {
            duration: 0,
        },
        responsive: true,
        maintainAspectRatio: false,
        scales: {
            x: { // defining min and max so hiding the dataset does not change scale range
                suggestedMin: 0,
                suggestedMax: chartTimeMax,
            },
            y: { // defining min and max so hiding the dataset does not change scale range
                suggestedMin: -100,
                suggestedMax: 100,
            },
        },
        plugins: {
            legend: {
                display: false
            },
        }
    }
});

/*** Helpers ***/

function refreshGraph() {
    ecgChart.update();
}

setInterval(refreshGraph, 500)

function onClearGraphClick() {
    ecgChart.data.labels = [];
    ecgChart.data.datasets[0].data = [];
    ecgChart.update();
    window.data = [];
    // Save start time
    let millis = Date.now();
    timeDataStart = millis * 0.001;
}

/**
 * 
 * @param {Int16Array} data 
 * @param {number[]} time 
 */
function ecgChartAddData(data, time) {
    /** @param {Int16Array} data */

    for (let i = 0; i < data.length; i++) {
        let sample = {x: time[i], y: data[i]}
        ecgChart.data.datasets[0].data.push(sample);
        window.data.push(sample);
    }
    while (ecgChart.data.datasets[0].data[ecgChart.data.datasets[0].data.length - 1].x - ecgChart.data.datasets[0].data[0].x > chartTimeMax) {
        ecgChart.data.datasets[0].data.shift();
    }
    ecgChart.options.scales['x'].min = Math.ceil(2 * ecgChart.data.datasets[0].data[0].x)/2;
    ecgChart.options.scales['x'].max = Math.max(chartTimeMax, Math.floor(2 * ecgChart.data.datasets[0].data[ecgChart.data.datasets[0].data.length - 1].x)/2);
}

function saveWindowData() {
    // Set filename
    let date = getFormattedTime(timeDataStart);
    date = date.replace(/:/g, "-").replace(/ /g, "_");
    const filename = bleDeviceName + "_" + date + '.csv';
    // Set header
    let csvContent = 'Time (s),ECG (A.U.)\r\n'
    window.data.forEach(element => {
        csvContent += element.x.toFixed(6).toString() + ',' + element.y.toString() + '\r\n'
    });
        
    let element = document.createElement('a');
    element.setAttribute('href', 'data:text;charset=utf-8,' + encodeURIComponent(csvContent));
    element.setAttribute('download', filename);
    element.style.display = 'none';

    document.body.appendChild(element);
    element.click();
    document.body.removeChild(element);
}

console.log("RUN JAVASCRIPT,  RUUUUUUUUNNNNN !!!!!");