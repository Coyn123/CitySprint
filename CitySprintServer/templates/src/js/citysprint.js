const canvas = document.getElementById('gameCanvas');
const context = canvas.getContext('2d');
const tileSize = 5; // Update the tileSize to match server
let gameMatrix = []; // Initialize the game matrix

const fullscrBtn = document.getElementById('fullscreenBtn');

const ws = new WebSocket("ws://localhost:9001");

console.log("WS was created");

ws.onopen = function (event) {
    console.log("WebSocket connection opened.");
    ws.send("Hello from client");
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

fullscrBtn.addEventListener("click", goFullScreen);

canvas.addEventListener("click", () => {
    const rect = canvas.getBoundingClientRect();
    //const x 
});

function changeGridPoint(x, y, color) {
    const message = `${x},${y},${color}`;
    ws.send(message);
    console.log(`Sent message to change grid point (${x}, ${y}) to color ${color}`);
}

function handleServerMessage(event) {
    console.log("Event Data (Raw): ", event.data);

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
