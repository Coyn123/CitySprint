const canvas = document.getElementById('gameCanvas');
const context = canvas.getContext('2d');
const tileSize = 2; // Update the tileSize to match server
let gameMatrix = []; // Initialize the game matrix

const fullscrBtn = document.getElementById('fullscreenBtn');

const ws = new WebSocket("ws://localhost:9001"); // For Development
// const ws = new WebSocket("ws://<server_ip>:9001"); // For Production

console.log("WS was created");

ws.onopen = function (event) {
    console.log("WebSocket connection opened.");
    //ws.send("Hello from client");
};

ws.onmessage = function (event) {
    if (event.data === "ping") {
        console.log("Received keep-alive ping from server.");
        return;
    }
    handleServerMessage(event);
};

ws.onclose = function (event) {
    console.log("WebSocket connection closed.");
};

ws.onerror = function (event) {
    console.error("WebSocket error: ", event);
};

function goFullScreen() {
    var canvas = document.getElementById("gameCanvas");
    if (canvas.requestFullScreen)
        canvas.requestFullScreen();
    else if (canvas.webkitRequestFullScreen)
        canvas.webkitRequestFullScreen();
    else if (canvas.mozRequestFullScreen)
        canvas.mozRequestFullScreen();
}

function clearBoard() {
    changeGridPoint(1000, 1000, "white"); 
    window.location.reload();
}

canvas.addEventListener("click", (e) => {
    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    const col = Math.floor(x / tileSize);
    const row = Math.floor(y / tileSize);

    changeGridPoint(col, row, "white");
});

function changeGridPoint(x, y, color) {
    let character = "troop";
    const message = `${x},${y},${color},${character}`;
    ws.send(message);
    console.log(`Sent message to change grid point (${x}, ${y}) to char ${character} with color ${color}`);
}

function handleServerMessage(event) {
    console.log("Event Data (Raw): ", event.data);
    if (!event.data) {
        return;
    }
    const updates = event.data.split(";");
    updates.forEach(update => {
        if (update.trim()) { // Ensure no empty segments
            const [x, y, color] = update.split(",");
            if (x !== undefined && y !== undefined && color !== undefined) {
                const xPos = parseInt(x);
                const yPos = parseInt(y);
                //console.log(`Updating tile at (${xPos}, ${yPos}) to color ${color}`); // Debugging

                // Update the game matrix
                if (!gameMatrix[yPos]) {
                    gameMatrix[yPos] = [];
                }
                gameMatrix[yPos][xPos] = color;

                // Redraw only the updated tile
                context.clearRect(xPos * tileSize, yPos * tileSize, tileSize, tileSize);
                context.fillStyle = color;
                context.fillRect(xPos * tileSize, yPos * tileSize, tileSize, tileSize);
            }
        }
    });
    //console.log("Updated game state: ", JSON.stringify(gameMatrix, null, 2)); // Debugging
}

// Initial setup to clear the canvas and fill with an initial color if needed
function initializeGameMatrix() {
    context.clearRect(0, 0, canvas.width, canvas.height);
    context.fillStyle = '#ffff'; // Initial background color
    context.fillRect(0, 0, canvas.width, canvas.height);
}

// Call this function to start the game
initializeGameMatrix();
