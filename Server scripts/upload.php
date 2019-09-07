<?php
header('Content-Type: application/json');

$received = file_get_contents('php://input');
$fileToWrite = date("d.m.Y-H:i:s") . ".jpg";

// Insert text to image
$jpg_image = imagecreatefromstring($received);
$text = date("d.m.Y H:i:s");
$font =  dirname(__FILE__) . '/arial.ttf';
$font_color = imagecolorallocate($jpg_image, 0, 0, 0);
$bg_color = imagecolorallocatealpha($jpg_image, 220, 220, 220, 100);

$font_size = 40;
$offset_x = 1090;
$offset_y = 85;
$text_width = 520;
$text_height = 55;
$text_rectangle_offset = 2;

imagefilledrectangle($jpg_image, $offset_x, $offset_y + $text_rectangle_offset, $offset_x + $text_width, $offset_y - $text_height, $bg_color);
imagettftext($jpg_image, $font_size, 0, $offset_x, $offset_y - 7, $font_color, $font, $text);
imagejpeg($jpg_image, $fileToWrite, 100);
imagedestroy($jpg_image);

// Send image to Cloudinary API
$headers = array("Content-Type:multipart/form-data");
$postfields = array("file" => new CURLFile($fileToWrite), "upload_preset" => "YYY");
$url = "XXX";

$ch = curl_init();
$options = array(
    CURLOPT_URL => $url,
    CURLOPT_HEADER => false,
    CURLOPT_POST => true,
    CURLOPT_HTTPHEADER => $headers,
    CURLOPT_POSTFIELDS => $postfields,
    CURLOPT_RETURNTRANSFER => true
);

curl_setopt_array($ch, $options);
$response = curl_exec($ch);

curl_close($ch);

// Get response from Cloudinary - image URL
$json = json_decode($response);
$imageUrlField = $json->url;

// Write image URL to blynk image widget
$blynkAuthToken = "YYY";
$blynkPin = "V1";
$url = "http://blynk-cloud.com/" . $blynkAuthToken . "/update/" . $blynkPin . "?urls=" . $imageUrlField;

$ch = curl_init();
$options = array(
    CURLOPT_URL => $url,
    CURLOPT_HEADER => false,
    CURLOPT_RETURNTRANSFER => true
);

curl_setopt_array($ch, $options);
$response = curl_exec($ch);
curl_close($ch);

// delete image file from directory
unlink($fileToWrite);

// return response
echo $response;